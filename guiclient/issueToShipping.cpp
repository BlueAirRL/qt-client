/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2014 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */


#include "issueToShipping.h"

#include <QSqlError>
#include <QVariant>

#include <metasql.h>

#include "xmessagebox.h"
#include "inputManager.h"
#include "distributeInventory.h"
#include "issueLineToShipping.h"
#include "mqlutil.h"
#include "reserveSalesOrderItem.h"
#include "shipOrder.h"
#include "storedProcErrorLookup.h"
#include "errorReporter.h"

#define DEBUG false

issueToShipping::issueToShipping(QWidget* parent, const char* name, Qt::WindowFlags fl)
    : XWidget(parent, name, fl),
      _captive(false)
{
  setupUi(this);

  connect(_ship,        SIGNAL(clicked()),                              this,         SLOT(sShip()));
  connect(_issueLine,   SIGNAL(clicked()),                              this,         SLOT(sIssueLineBalance()));
  connect(_issueAll,    SIGNAL(clicked()),                              this,         SLOT(sIssueAllBalance()));
  connect(_issueStock,  SIGNAL(clicked()),                              this,         SLOT(sIssueStock()));
  connect(_order,       SIGNAL(valid(bool)),                            this,         SLOT(sFillList()));
  connect(_returnStock, SIGNAL(clicked()),                              this,         SLOT(sReturnStock()));
  connect(_bcFind,      SIGNAL(clicked()),                              this,         SLOT(sBcFind()));
  connect(_soitem,      SIGNAL(itemSelectionChanged()),                 this,         SLOT(sHandleButtons()));
  connect(_soitem,      SIGNAL(populateMenu(QMenu*,QTreeWidgetItem *)), this,         SLOT(sPopulateMenu(QMenu *)));
  connect(_warehouse,   SIGNAL(newID(int)),                             this,         SLOT(sFillList()));

  _order->setAllowedStatuses(OrderLineEdit::Open);
  _order->setAllowedTypes(OrderLineEdit::Sales |
                          OrderLineEdit::Transfer);
  _order->setLockSelected(true);

  _ship->setEnabled(_privileges->check("ShipOrders"));

  omfgThis->inputManager()->notify(cBCItem, this, this, SLOT(sCatchItemid(int)));
  omfgThis->inputManager()->notify(cBCItemSite, this, this, SLOT(sCatchItemsiteid(int)));
  omfgThis->inputManager()->notify(cBCSalesOrder, this, this, SLOT(sCatchSoheadid(int)));
  omfgThis->inputManager()->notify(cBCSalesOrderLineItem, this, this, SLOT(sCatchSoitemid(int)));
  omfgThis->inputManager()->notify(cBCTransferOrder, this, this, SLOT(sCatchToheadid(int)));
  omfgThis->inputManager()->notify(cBCTransferOrderLineItem, this, this, SLOT(sCatchToitemid(int)));
  omfgThis->inputManager()->notify(cBCWorkOrder, this, this, SLOT(sCatchWoid(int)));

  if (_metrics->boolean("EnableSOReservationsByLocation"))
  {
    _lineItemsLit->setText("Line Items with Reservations:");

    _soitem->addColumn(tr("#"),                _seqColumn,   Qt::AlignCenter, true,  "linenumber");
    _soitem->addColumn(tr("Item/Location"),    _itemColumn,  Qt::AlignLeft,   true,  "item_number");
    _soitem->addColumn(tr("Desc./LotSerial"),   -1,          Qt::AlignLeft,   true,  "itemdescrip");
    _soitem->addColumn(tr("Site"),             _whsColumn,   Qt::AlignCenter, true,  "warehous_code");
    _soitem->addColumn(tr("Sched. Date"),      _qtyColumn,   Qt::AlignRight,  true,  "scheddate");
    _soitem->addColumn(tr("UOM"),              _uomColumn,   Qt::AlignLeft,   true,  "uom_name");
    _soitem->addColumn(tr("Ordered/Reserved"), _qtyColumn,   Qt::AlignRight,  true,  "qtyord");
    _soitem->addColumn(tr("Shipped"),          _qtyColumn,   Qt::AlignRight,  true,  "qtyshipped");
    _soitem->addColumn(tr("Returned"),         _qtyColumn,   Qt::AlignRight,  false, "qtyreturned");
    _soitem->addColumn(tr("Balance"),          _qtyColumn,   Qt::AlignRight,  false, "balance");
    _soitem->addColumn(tr("At Shipping"),      _qtyColumn,   Qt::AlignRight,  true,  "atshipping");
  }
  else
  {
    _soitem->addColumn(tr("#"),           _seqColumn,   Qt::AlignCenter, true,  "linenumber");
    _soitem->addColumn(tr("Item Number"), _itemColumn,  Qt::AlignLeft,   true,  "item_number");
    _soitem->addColumn(tr("Description"),  -1,          Qt::AlignLeft,   true,  "itemdescrip");
    _soitem->addColumn(tr("Site"),        _whsColumn,   Qt::AlignCenter, true,  "warehous_code");
    _soitem->addColumn(tr("Sched. Date"), _qtyColumn,   Qt::AlignRight,  true,  "scheddate");
    _soitem->addColumn(tr("UOM"),         _uomColumn,   Qt::AlignLeft,   true,  "uom_name");
    _soitem->addColumn(tr("Ordered"),     _qtyColumn,   Qt::AlignRight,  true,  "qtyord");
    _soitem->addColumn(tr("Shipped"),     _qtyColumn,   Qt::AlignRight,  true,  "qtyshipped");
    _soitem->addColumn(tr("Returned"),    _qtyColumn,   Qt::AlignRight,  false, "qtyreturned");
    _soitem->addColumn(tr("Balance"),     _qtyColumn,   Qt::AlignRight,  false, "balance");
    _soitem->addColumn(tr("At Shipping"), _qtyColumn,   Qt::AlignRight,  true,  "atshipping");
  }

  _soitem->setSelectionMode(QAbstractItemView::ExtendedSelection);

  _order->setFromSitePrivsEnforced(true);
  _order->setFocus();

  _bcQty->setValidator(omfgThis->qtyVal());

  if (!_metrics->boolean("MultiWhs"))
  {
    _warehouseLit->hide();
    _warehouse->hide();
  }
  
  if(_metrics->boolean("EnableSOReservations") || _metrics->boolean("DisallowNegativeInventory"))
  {
    _requireInventory->setForgetful(true);
    _requireInventory->setChecked(true);
    _requireInventory->setEnabled(false);
  }

  _transDate->setEnabled(_privileges->check("AlterTransactionDates"));
  _transDate->setDate(omfgThis->dbDate());

  // Until 9063 implemented:
  _issueByGroup->hide();
  _onlyReserved->hide();
}

issueToShipping::~issueToShipping()
{
  // no need to delete child widgets, Qt does it all for us
}

void issueToShipping::languageChange()
{
  retranslateUi(this);
}

enum SetResponse issueToShipping::set(const ParameterList &pParams)
{
  XWidget::set(pParams);
  QVariant param;
  bool     valid;

  param = pParams.value("sohead_id", &valid);
  if (valid)
  {
    _order->setId(param.toInt(), "SO");
    if (_order->isValid())
    {
      _order->setEnabled(false);
      _captive = true;
    }
  }

  param = pParams.value("tohead_id", &valid);
  if (valid)
  {
    _order->setId(param.toInt(), "TO");
    if (_order->isValid())
    {
      _order->setEnabled(false);
      _captive = true;
    }
  }

  return NoError;
}

void issueToShipping::sHandleButtons()
{
  if (_soitem->currentItem() && _soitem->currentItem()->rawValue("atshipping").toDouble() > 0)
    _returnStock->setEnabled(true);
  else
    _returnStock->setEnabled(false);
}

void issueToShipping::sCatchSoheadid(int pSoheadid)
{
  _order->setId(pSoheadid, "SO");
  _soitem->selectAll();
}

void issueToShipping::sCatchSoitemid(int pSoitemid)
{
  XSqlQuery issueCatchSoitemid;
  issueCatchSoitemid.prepare( "SELECT coitem_cohead_id "
             "FROM coitem "
             "WHERE (coitem_id=:sohead_id);" );
  issueCatchSoitemid.bindValue(":sohead_id", pSoitemid);
  issueCatchSoitemid.exec();
  if (issueCatchSoitemid.first())
  {
    _order->setId(issueCatchSoitemid.value("coitem_cohead_id").toInt(), "SO");
    _soitem->clearSelection();
    _soitem->setId(pSoitemid);
    sIssueStock();
  }
  else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                                issueCatchSoitemid, __FILE__, __LINE__))
  {
    return;
  }
}

void issueToShipping::sCatchToheadid(int pToheadid)
{
  _order->setId(pToheadid, "TO");
  _soitem->selectAll();
}

void issueToShipping::sCatchToitemid(int porderitemid)
{
  XSqlQuery issueCatchToitemid;
  issueCatchToitemid.prepare( "SELECT toitem_tohead_id "
             "FROM toitem "
             "WHERE (toitem_id=:tohead_id);" );
  issueCatchToitemid.bindValue(":tohead_id", porderitemid);
  issueCatchToitemid.exec();
  if (issueCatchToitemid.first())
  {
    _order->setId(issueCatchToitemid.value("toitem_tohead_id").toInt(), "TO");
    _soitem->clearSelection();
    _soitem->setId(porderitemid);
    sIssueStock();
  }
  else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                                issueCatchToitemid, __FILE__, __LINE__))
  {
    return;
  }
}

void issueToShipping::sCatchItemsiteid(int pItemsiteid)
{
  XSqlQuery issueCatchItemsiteid;
  issueCatchItemsiteid.prepare("SELECT orderitem_id "
            "FROM orderitem "
            "WHERE ((orderitem_itemsite_id=:itemsite) "
            "   AND (orderitem_orderhead_type=:ordertype) "
            "   AND (orderitem_orderhead_id=:orderid));");
  issueCatchItemsiteid.bindValue(":itemsite",  pItemsiteid);
  issueCatchItemsiteid.bindValue(":ordertype", _order->type());
  issueCatchItemsiteid.bindValue(":orderid",   _order->id());
  issueCatchItemsiteid.exec();
  if (issueCatchItemsiteid.first())
  {
    _soitem->clearSelection();
    _soitem->setId(issueCatchItemsiteid.value("orderitem_id").toInt());
    sIssueStock();
  }
  else
    audioReject();
}

void issueToShipping::sCatchItemid(int pItemid)
{
  XSqlQuery issueCatchItemid;
  issueCatchItemid.prepare( "SELECT orderitem_id "
             "FROM orderitem, itemsite "
             "WHERE ((orderitem_itemsite_id=itemsite_id)"
             "  AND  (itemsite_item_id=:item_id)"
             "   AND  (orderitem_orderhead_type=:ordertype) "
             "   AND  (orderitem_orderhead_id=:orderid));");
  issueCatchItemid.bindValue(":item_id",   pItemid);
  issueCatchItemid.bindValue(":ordertype", _order->type());
  issueCatchItemid.bindValue(":orderid",   _order->id());
  issueCatchItemid.exec();
  if (issueCatchItemid.first())
  {
    _soitem->clearSelection();
    _soitem->setId(issueCatchItemid.value("orderitem_id").toInt());
    sIssueStock();
  }
  else
    audioReject();
}

void issueToShipping::sCatchWoid(int pWoid)
{
  XSqlQuery issueCatchWoid;
  if (_order->isSO())
  {
    issueCatchWoid.prepare( " SELECT coitem_cohead_id, coitem_id"
               "  FROM coitem"
               " WHERE ((coitem_order_id=:wo_id)"
               "   AND  (coitem_cohead_id=:sohead_id));" );
    issueCatchWoid.bindValue(":wo_id",     pWoid);
    issueCatchWoid.bindValue(":sohead_id", _order->id());
    issueCatchWoid.exec();
    if (issueCatchWoid.first())
    {
      _order->setId(issueCatchWoid.value("coitem_cohead_id").toInt());
      _soitem->clearSelection();
      _soitem->setId(issueCatchWoid.value("coitem_id").toInt());
      sIssueStock();
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                                  issueCatchWoid, __FILE__, __LINE__))
    {
      return;
    }
  }
  else
    audioReject();
}

void issueToShipping::sPopulateMenu(QMenu *pMenu)
{
  if (_metrics->boolean("EnableSOReservations"))
  {
    QAction *menuItem;
    menuItem = pMenu->addAction(tr("Unreserve Stock"), this, SLOT(sUnreserveStock()));
    menuItem->setEnabled(_privileges->check("MaintainReservations"));
    menuItem = pMenu->addAction(tr("Reserve Stock..."), this, SLOT(sReserveStock()));
    menuItem->setEnabled(_privileges->check("MaintainReservations"));
    menuItem = pMenu->addAction(tr("Reserve Line Balance"), this, SLOT(sReserveLineBalance()));
    menuItem->setEnabled(_privileges->check("MaintainReservations"));
  }
}

void issueToShipping::sIssueStock()
{
  bool update  = false;
  QList<XTreeWidgetItem*> selected = _soitem->selectedItems();
  for (int i = 0; i < selected.size(); i++)
  {
    ParameterList params;
    params.append("order_id",   ((XTreeWidgetItem*)selected[i])->id());
    params.append("order_type", _order->type());
    params.append("transTS",    _transDate->date());

    if(_requireInventory->isChecked())
      params.append("requireInventory");
    
    issueLineToShipping newdlg(this, "", true);
    if (newdlg.set(params) == NoError && newdlg.exec() != XDialog::Rejected)
      update = true;
  }

  if (update)
    sFillList();
}

bool issueToShipping::sufficientItemInventory(int porderitemid)
{
  XSqlQuery issueufficientItemInventory;
  if(_requireInventory->isChecked() ||
     (_order->isSO() && _metrics->boolean("EnableSOReservations")))
  {
    issueufficientItemInventory.prepare("SELECT sufficientInventoryToShipItem(:ordertype, :itemid) AS result;");
    issueufficientItemInventory.bindValue(":itemid", porderitemid);
    issueufficientItemInventory.bindValue(":ordertype", _order->type());
    issueufficientItemInventory.exec();
    if (issueufficientItemInventory.first())
    {
      int result = issueufficientItemInventory.value("result").toInt();
      if (result < 0)
      {
        ParameterList errp;
        if (_order->isSO())
          errp.append("soitem_id", porderitemid);
        else if (_order->isTO())
          errp.append("toitem_id", porderitemid);

        QString errs = "<? if exists(\"soitem_id\") ?>"
                       "SELECT item_number, warehous_code "
                       "  FROM coitem, item, itemsite, whsinfo "
                       " WHERE ((coitem_itemsite_id=itemsite_id)"
                       "   AND  (itemsite_item_id=item_id)"
                       "   AND  (itemsite_warehous_id=warehous_id)"
                       "   AND  (coitem_id=<? value(\"soitem_id\") ?>));"
                       "<? elseif exists(\"toitem_id\")?>"
                       "SELECT item_number, tohead_srcname AS warehous_code "
                       "  FROM toitem, tohead, item "
                       " WHERE ((toitem_item_id=item_id)"
                       "   AND  (toitem_tohead_id=tohead_id)"
                       "   AND  (toitem_id=<? value(\"toitem_id\") ?>));"
                       "<? endif ?>" ;
         MetaSQLQuery errm(errs);
         issueufficientItemInventory = errm.toQuery(errp);
         if (! issueufficientItemInventory.first() && issueufficientItemInventory.lastError().type() != QSqlError::NoError)
           ErrorReporter::error(QtCriticalMsg, this, tr("Insufficient Inventory To Ship"),
                              issueufficientItemInventory, __FILE__, __LINE__);

         ErrorReporter::error(QtCriticalMsg, this, tr("Insufficient Inventory To Ship"),
                                  storedProcErrorLookup("sufficientInventoryToShipItem", result)
                                  .arg(issueufficientItemInventory.value("item_number").toString())
                                  .arg(issueufficientItemInventory.value("warehous_code").toString()),
                                  __FILE__, __LINE__);
         return false;
       }
     }
     else if (ErrorReporter::error(QtCriticalMsg, this, tr("Insufficient Inventory To Ship"),
                                  issueufficientItemInventory, __FILE__, __LINE__))
     {
       return false;
     }
   }

  return true;
}

bool issueToShipping::sufficientInventory(int porderheadid)
{
  XSqlQuery issueufficientInventory;
  if (_requireInventory->isChecked() ||
      (_order->isSO() && _metrics->boolean("EnableSOReservations")))
  {
    issueufficientInventory.prepare("SELECT sufficientInventoryToShipOrder(:ordertype, :orderid) AS result;");
    issueufficientInventory.bindValue(":orderid", porderheadid);
    issueufficientInventory.bindValue(":ordertype", _order->type());
    issueufficientInventory.exec();
    if (issueufficientInventory.first())
    {
      int result = issueufficientInventory.value("result").toInt();
      if (result < 0)
      {
        ErrorReporter::error(QtCriticalMsg, this, tr("Insufficient Inventory To Ship"),
                               storedProcErrorLookup("sufficientInventoryToShipOrder", result),
                               __FILE__, __LINE__);
        return false;
      }
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Insufficient Inventory To Ship"),
                                  issueufficientInventory, __FILE__, __LINE__))
    {
      return false;
    }
  }

  return true;
}

void issueToShipping::sIssueLineBalance()
{
  bool refresh = false;

  QList<XTreeWidgetItem*> selected = _soitem->selectedItems();
  for (int i = 0; i < selected.size(); i++)
  {
    XTreeWidgetItem *cursor = (XTreeWidgetItem*)selected[i];
    if (sIssueLineBalance(cursor->id(), cursor->altId()))
      refresh = true;
    else
      break;
  }

  if (refresh)
    sFillList();
}

bool issueToShipping::sIssueLineBalance(int id, int altId)
{
  if (DEBUG)
    qDebug() << "issueToShipping::sIssueLineBalance(" << id << ", " << altId;
    
  if (altId == 0) // Not a Job costed item
  {
    if (! sufficientItemInventory(id))
      return false;
  }
  int invhistid = 0;
  int itemlocSeries;
  int itemlocdistId;
  int itemsiteId;
  double balance;
  bool controlled;


  XSqlQuery rollback;
  rollback.prepare("ROLLBACK;");

  // Stage cleanup function to be called on error
  XSqlQuery cleanup;
  cleanup.prepare("SELECT deleteitemlocseries(:itemlocSeries, TRUE);");

  XSqlQuery issue;

  // Get parent series id
  XSqlQuery parentItemlocdist;
  XSqlQuery parentSeries;
  parentSeries.prepare("SELECT NEXTVAL('itemloc_series_seq') AS result, "
                       "  calcIssueToShippingLineBalance(:orderType, :orderitemId) AS balance, "
                       "  isControlledItemsite(orderitem_itemsite_id) AS controlled, "
                       "  orderitem_itemsite_id AS itemsite_id "
                       "FROM orderitem "
                       "WHERE orderitem_orderhead_type = :orderType "
                       " AND orderitem_id = :orderitemId;");
  parentSeries.bindValue(":orderType", _order->type());
  parentSeries.bindValue(":orderitemId", id);
  parentSeries.exec();
  if (parentSeries.first() && parentSeries.value("result").toInt() > 0)
  {
    if (parentSeries.size() > 1)
    {
      ErrorReporter::error(QtCriticalMsg, this, tr("Retrieving from orderitem table returned more than one row."),
        parentSeries, __FILE__, __LINE__);
      return false;
    }

    itemlocSeries = parentSeries.value("result").toInt();
    balance = parentSeries.value("balance").toDouble();
    controlled = parentSeries.value("controlled").toBool();
    itemsiteId = parentSeries.value("itemsite_id").toInt();
    cleanup.bindValue(":itemlocSeries", itemlocSeries);

    if (DEBUG)
    {
      qDebug() << "issueToShipping::sIssueLineBalance itemlocSeries: " << itemlocSeries;
      qDebug() << "issueToShipping::sIssueLineBalance balance: " << balance;
      qDebug() << "issueToShipping::sIssueLineBalance controlled: " << controlled;  
      qDebug() << "issueToShipping::sIssueLineBalance itemsiteId: " << itemsiteId;
    }
  }
  else
  {
    ErrorReporter::error(QtCriticalMsg, this, tr("Could not find this orderitem_id in the orderitem table."),
      parentSeries, __FILE__, __LINE__);
    return false;
  }

  // If this is a lot/serial controlled job item, we need to post production first
  if (altId == 1)
  {
    // Before postSoItemProduction, if controlled: create the itemlocdist record and call distributeInventory::SeriesAdjust
    if (controlled)
    {
      parentItemlocdist.prepare("SELECT createItemlocdistParent(:itemsite_id, :qty, :orderType, :orderitemId, :itemlocSeries, NULL, NULL, 'RM') AS result;");
      parentItemlocdist.bindValue(":itemsite_id", itemsiteId);
      parentItemlocdist.bindValue(":qty", balance * -1);
      parentItemlocdist.bindValue(":orderType", _order->type());
      parentItemlocdist.bindValue(":orderitemId", id);
      parentItemlocdist.bindValue(":itemlocSeries", itemlocSeries);
      parentItemlocdist.exec();
      if (parentItemlocdist.first())
      {
        // Store the resulting itemlocdist_id for use when calling createItemlocdistParent below 
        itemlocdistId = parentItemlocdist.value("result").toInt();

        if (distributeInventory::SeriesAdjust(itemlocSeries, this, QString(), QDate(), QDate(), true)
          == XDialog::Rejected)
        {
          rollback.exec();
          cleanup.exec();
          QMessageBox::information( this, tr("Issue to Shipping"), tr("Issue Canceled") );
          return false;
        }
      }
      else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Creating itemlocdist Records"),
                                parentItemlocdist, __FILE__, __LINE__))
      {
        return false;
      }
    }

    issue.exec("BEGIN;");

    XSqlQuery prod;
    prod.prepare("SELECT postSoItemProduction(:soitem_id, :qty, :ts, :itemlocSeries, TRUE) AS result;");
    prod.bindValue(":soitem_id", id);
    prod.bindValue(":qty", balance);
    prod.bindValue(":ts", _transDate->date());
    prod.bindValue(":itemlocSeries", itemlocSeries);
    prod.exec();
    if (prod.first())
    {
      int result = prod.value("result").toInt();

      if (result < 0 || result != itemlocSeries)
      {
        rollback.exec();
        cleanup.exec();
        ErrorReporter::error(QtCriticalMsg, this, tr("Error Posting Production"),
                               storedProcErrorLookup("postProduction", itemlocSeries),
                               __FILE__, __LINE__);
        return false;
      }
        
      // Need to get the inventory history id so we can auto reverse the distribution when issuing
      prod.prepare("SELECT invhist_id "
                   "FROM invhist "
                   "WHERE ((invhist_series = :itemlocseries) "
                   " AND (invhist_transtype = 'RM')); ");
      prod.bindValue(":itemlocseries" , itemlocSeries);
      prod.exec();
      if (prod.first())
        invhistid = prod.value("invhist_id").toInt();
      else
      {
        rollback.exec();
        cleanup.exec();
        ErrorReporter::error(QtCriticalMsg, this, tr("Error Occurred"),
                             tr("Inventory history not found")
                             .arg(windowTitle()),__FILE__,__LINE__);
        return false;
      }
    }
    else if (prod.lastError().type() != QSqlError::NoError)
    {
      rollback.exec();
      cleanup.exec();
      ErrorReporter::error(QtCriticalMsg, this, tr("Error Posting Production"),
                           prod, __FILE__, __LINE__);
      return false;
    }
  }

  // Before issueToShipping, if controlled item: create the parent itemlocdist record, call distributeInventory::seriesAdjust
  if (controlled)
  {
    parentItemlocdist.prepare("SELECT createItemlocdistParent(:itemsite_id, :qty, :orderType, :orderitemId, :itemlocSeries, "
      ":invhistId, :itemlocdistId, 'SH') AS result;");
    parentItemlocdist.bindValue(":itemsite_id", itemsiteId);
    parentItemlocdist.bindValue(":qty", balance * -1);
    parentItemlocdist.bindValue(":orderType", _order->type());
    parentItemlocdist.bindValue(":orderitemId", id);
    parentItemlocdist.bindValue(":itemlocSeries", itemlocSeries);
    if (invhistid > 0)
      parentItemlocdist.bindValue(":invhistId", invhistid);
    if (itemlocdistId > 0)
      parentItemlocdist.bindValue(":itemlocdistId", itemlocdistId);
    parentItemlocdist.exec();
    if (parentItemlocdist.first())
    {
      if (distributeInventory::SeriesAdjust(itemlocSeries, this, QString(), QDate(), QDate(), true)
        == XDialog::Rejected)
      {
        rollback.exec();
        cleanup.exec();
        QMessageBox::information( this, tr("Issue to Shipping"), tr("Issue Canceled") );
        return false;
      }
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Creating itemlocdist Records"),
                              parentItemlocdist, __FILE__, __LINE__))
    {
      return false;
    }
  }

  // Finally, issue to shipping, wrap the remaining sql (if Job costed, transaction already began). 
  issue.exec("BEGIN;"); // TODO - remove this after issueToShipping is no longer returning negative error codes.
  issue.prepare("SELECT issueToShipping(:ordertype::text, :lineitem_id, :qty, :itemlocSeries, :ts, "
    ":invhist_id, false, true) AS result;");
  issue.bindValue(":ordertype",   _order->type());
  issue.bindValue(":lineitem_id", id);
  issue.bindValue(":qty",         balance);
  issue.bindValue(":itemlocSeries", itemlocSeries);
  issue.bindValue(":ts",          _transDate->date());
  if (invhistid > 0)
    issue.bindValue(":invhist_id", invhistid);
  issue.exec();

  if (issue.first())
  {
    int result = issue.value("result").toInt();
    if (result < 0 || result != itemlocSeries)
    {
      cleanup.exec();
      rollback.exec();
      ErrorReporter::error(QtCriticalMsg, this, tr("Error Issuing Item"),
                             storedProcErrorLookup("issueToShipping", result),
                             __FILE__, __LINE__);
      return false;
    }

  	// If Transfer Order then insert special pre-assign records for the lot/serial#
  	// so they are available when the Transfer Order is received
  	if (_order->type() == "TO")
  	{
        XSqlQuery lsdetail;
        lsdetail.prepare("INSERT INTO lsdetail "
  	                     " (lsdetail_itemsite_id, lsdetail_created, lsdetail_source_type, "
  	  				           "  lsdetail_source_id, lsdetail_source_number, lsdetail_ls_id, lsdetail_qtytoassign, "
                         "  lsdetail_expiration, lsdetail_warrpurc ) "
  					             "SELECT invhist_itemsite_id, NOW(), 'TR', "
  					             "   :orderitemid, invhist_ordnumber, invdetail_ls_id, (invdetail_qty * -1.0), "
                         "   invdetail_expiration, invdetail_warrpurc "
  					             "FROM invhist JOIN invdetail ON (invdetail_invhist_id=invhist_id) "
  					             "WHERE invhist_series=:itemlocseries;");
        lsdetail.bindValue(":orderitemid", id);
        lsdetail.bindValue(":itemlocseries", result);
        lsdetail.exec();
        if (lsdetail.lastError().type() != QSqlError::NoError)
        {
          rollback.exec();
          cleanup.exec();
          ErrorReporter::error(QtCriticalMsg, this, tr("Error Issuing Item"),
                               lsdetail, __FILE__, __LINE__);
          return false;
        }
  	}
	
    issue.exec("COMMIT;");
  }
  else if (issue.lastError().type() != QSqlError::NoError)
  {
    rollback.exec();
    cleanup.exec();
    ErrorReporter::error(QtCriticalMsg, this, tr("Error Issuing Item"),
                         issue, __FILE__, __LINE__);
    return false;
  }
  return true;
}

void issueToShipping::sIssueAllBalance()
{
  bool refresh = false;
  int orderid = _order->id();

  if (! sufficientInventory(orderid))
    return;

  for (int i = 0; i < _soitem->topLevelItemCount(); i++)
  {
    XTreeWidgetItem *cursor = (XTreeWidgetItem*)_soitem->topLevelItem(i);
    if (sIssueLineBalance(cursor->id(),cursor->altId()))
      refresh = true;
    // attempt to issue all lines
    //else
    //  break;
  }

  if (refresh)
    sFillList();
}

void issueToShipping::sReturnStock()
{
  XSqlQuery issueReturnStock;
  QList<XTreeWidgetItem*> selected = _soitem->selectedItems();
  for (int i = 0; i < selected.size(); i++)
  {
    XTreeWidgetItem *cursor = (XTreeWidgetItem*)selected[i];
    
    XSqlQuery rollback;
    rollback.prepare("ROLLBACK;");

    issueReturnStock.exec("BEGIN;");
    issueReturnStock.prepare("SELECT returnItemShipments(:ordertype, :soitem_id, 0, :ts) AS result;");
    issueReturnStock.bindValue(":ordertype", _order->type());
    issueReturnStock.bindValue(":soitem_id", cursor->id());
    issueReturnStock.bindValue(":ts",        _transDate->date());
    issueReturnStock.exec();
    if (issueReturnStock.first())
    {
      int result = issueReturnStock.value("result").toInt();
      if (result < 0)
      {
        rollback.exec();
        ErrorReporter::error(QtCriticalMsg, this, tr("Error Returning Stock"),
                               storedProcErrorLookup("returnItemShipments", result),
                               __FILE__, __LINE__);
          return;
      }
      else if (distributeInventory::SeriesAdjust(result, this) == XDialog::Rejected)
      {
        rollback.exec();
        QMessageBox::information( this, tr("Issue to Shipping"), tr("Return Canceled") );
        return;
      }      
      issueReturnStock.exec("COMMIT;"); 
    }
    else if (issueReturnStock.lastError().type() != QSqlError::NoError)
    {
      rollback.exec();
      ErrorReporter::error(QtCriticalMsg, this, tr("Error Returning Stock"),
                           issueReturnStock, __FILE__, __LINE__);
      return;
    }
  }

  sFillList();
}

void issueToShipping::sShip()
{
  XSqlQuery issueShip;
  issueShip.prepare( "SELECT getOpenShipmentId(:ordertype, :order_id, :warehous_id) AS shiphead_id;" );
  issueShip.bindValue(":order_id",  _order->id());
  issueShip.bindValue(":ordertype", _order->type());
  issueShip.bindValue(":warehous_id", _warehouse->id());

  issueShip.exec();
  if ( (issueShip.first()) && (issueShip.value("shiphead_id").toInt() > 0) )
  {
    // Reset _order so that lock is released prior to shipping and potentially auto receiving
    // to avoid locking conflicts
    _order->setId(-1);
    _order->setFocus();

    ParameterList params;
    params.append("shiphead_id", issueShip.value("shiphead_id").toInt());
    params.append("transdate", _transDate->date());

    shipOrder newdlg(this, "", true);
    if (newdlg.set(params) == NoError && newdlg.exec() != XDialog::Rejected)
    {
      _transDate->setDate(omfgThis->dbDate());
    }
  }
  else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Issuing Item"),
                                issueShip, __FILE__, __LINE__))
  {
    return;
  }
  else
  {
    QMessageBox::information( this, tr("Cannot Ship Order"),
                              tr("<p>You must issue some amount of Stock to "
                                 "this Order before you may ship it.") );
    return;
  }
  if (_captive)
    close();
}

void issueToShipping::sReserveStock()
{
  QList<XTreeWidgetItem *> selected = _soitem->selectedItems();
  for (int i = 0; i < selected.size(); i++)
  {
    ParameterList params;
    params.append("soitem_id", ((XTreeWidgetItem *)(selected[i]))->id());
    
    reserveSalesOrderItem newdlg(this, "", true);
    newdlg.set(params);
    newdlg.exec();
  }
  
  sFillList();
}

void issueToShipping::sReserveLineBalance()
{
  XSqlQuery reserveSales;
  reserveSales.prepare("SELECT reserveSoLineBalance(:soitem_id) AS result;");
  QList<XTreeWidgetItem *> selected = _soitem->selectedItems();
  for (int i = 0; i < selected.size(); i++)
  {
    reserveSales.bindValue(":soitem_id", ((XTreeWidgetItem *)(selected[i]))->id());
    reserveSales.exec();
    if (reserveSales.first())
    {
      int result = reserveSales.value("result").toInt();
      if (result < 0)
      {
        ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                               storedProcErrorLookup("reserveSoLineBalance", result),
                               __FILE__, __LINE__);
        return;
      }
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                                  reserveSales, __FILE__, __LINE__))
    {
      return;
    }
  }
  
  sFillList();
}

void issueToShipping::sUnreserveStock()
{
  XSqlQuery unreserveSales;
  unreserveSales.prepare("SELECT unreserveSoLineQty(:soitem_id) AS result;");
  QList<XTreeWidgetItem *> selected = _soitem->selectedItems();
  for (int i = 0; i < selected.size(); i++)
  {
    unreserveSales.bindValue(":soitem_id", ((XTreeWidgetItem *)(selected[i]))->id());
    unreserveSales.exec();
    if (unreserveSales.first())
    {
      int result = unreserveSales.value("result").toInt();
      if (result < 0)
      {
        ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                               storedProcErrorLookup("unreservedSoLineQty", result),
                               __FILE__, __LINE__);
        return;
      }
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                                  unreserveSales, __FILE__, __LINE__))
    {
      return;
    }
  }
  
  sFillList();
}

void issueToShipping::sFillList()
{
  XSqlQuery issueFillList;
  ParameterList listp;
  if (_order->id() < 0)
  {
    _soitem->clear();
    _order->setFocus();
    return;
  }
  else if (_order->isSO())
  {
    issueFillList.prepare("SELECT soHoldType(cohead_id) AS holdtype "
                          "FROM cohead "
                          "WHERE (cohead_id=:sohead_id);" );
    issueFillList.bindValue(":sohead_id", _order->id());
    issueFillList.exec();
    if (issueFillList.first())
    {
      if (issueFillList.value("holdtype").toString() == "C")
      {
        QMessageBox::critical( this, tr("Cannot Issue Stock"),
                              storedProcErrorLookup("issuetoshipping", -12));
        _order->setId(-1);
        _order->setFocus();
      }
      else if (issueFillList.value("holdtype").toString() == "P")
      {
        QMessageBox::critical( this, tr("Cannot Issue Stock"),
                              storedProcErrorLookup("issuetoshipping", -13));
        _order->setId(-1);
        _order->setFocus();
      }
      else if (issueFillList.value("holdtype").toString() == "R")
      {
        QMessageBox::critical( this, tr("Cannot Issue Stock"),
                              storedProcErrorLookup("issuetoshipping", -14));
        _order->setId(-1);
        _order->setFocus();
      }
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                                  issueFillList, __FILE__, __LINE__))
    {
      return;
    }
    else
    {
      _order->setId(-1);
      _order->setFocus();
      return;
    }

    listp.append("sohead_id", _order->id());
  }
  else if (_order->isTO())
  {
    listp.append("tohead_id", _order->id());
  }
  else
  {
    ErrorReporter::error(QtCriticalMsg, this, tr("Error Occurred"),
                         tr("Window:%1:\n Unrecognized order type %2")
                         .arg(windowTitle())
                         .arg(_order->type()),__FILE__,__LINE__);
    return;
  }

  listp.append("ordertype", _order->type());
  listp.append("warehous_id", _warehouse->id());

  if (_metrics->boolean("EnableSOReservationsByLocation"))
    listp.append("includeReservations");
  
  MetaSQLQuery listm = mqlLoad("issueToShipping", "detail");
  XSqlQuery listq = listm.toQuery(listp);
  _soitem->populate(listq, true);
  _soitem->expandAll();

  if (listq.first())
  {
    _shipment->setText(listq.value("shiphead_number").toString());
  }
  if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Item Information"),
                                listq, __FILE__, __LINE__))
  {
    return;
  }
}

void issueToShipping::sBcFind()
{
  XSqlQuery issueBcFind;
  if (_bc->text().isEmpty())
  {
    QMessageBox::warning(this, tr("No Bar Code scanned"),
                         tr("<p>Cannot search for Items by Bar Code without a "
                            "Bar Code."));
    _bc->setFocus();
    return;
  }

  // find item that matches barcode and is a line item in this order.
  // then call issueLineToShipping passing in params to preset and
  // run the issue button.
  // TODO: can we make this an orderitem select?
  QString sql = "<? if exists(\"sohead_id\") ?>"
                "SELECT coitem_id AS lineitem_id, noNeg(noNeg(coitem_qtyord - coitem_qtyshipped + coitem_qtyreturned) - COALESCE(SUM(shipitem_qty), 0)) AS balance"
                "  FROM itemsite, item,"
                "       coitem LEFT OUTER JOIN"
                "        ( shipitem JOIN shiphead"
                "          ON ( (shipitem_shiphead_id=shiphead_id)"
                "               AND (NOT shiphead_shipped"
                "               AND shiphead_order_type=<? value(\"ordertype\") ?>) )"
                "        ) ON  (shipitem_orderitem_id=coitem_id) "
                " WHERE ((coitem_itemsite_id=itemsite_id)"
                "   AND  (itemsite_item_id=item_id)"
                "   AND  (coitem_status NOT IN ('C', 'X'))"
                "   AND  (coitem_cohead_id=<? value(\"sohead_id\") ?>)"
                "   AND  (item_upccode=<? value(\"bc\") ?>))"
                " GROUP BY coitem_id, coitem_qtyord, coitem_qtyshipped, coitem_qtyreturned; "
                "<? elseif exists(\"tohead_id\") ?>"
                "SELECT toitem_id AS lineitem_id, noNeg(noNeg(toitem_qty_ordered - toitem_qty_shipped) - COALESCE(SUM(shipitem_qty), 0)) AS balance"
                "  FROM item,"
                "       toitem LEFT OUTER JOIN"
                "        ( shipitem JOIN shiphead"
                "          ON ( (shipitem_shiphead_id=shiphead_id)"
                "               AND (NOT shiphead_shipped"
                "               AND shiphead_order_type=<? value(\"ordertype\") ?>) )"
                "        ) ON  (shipitem_orderitem_id=toitem_id) "
                " WHERE ((toitem_item_id=item_id)"
                "   AND  (toitem_status NOT IN ('C', 'X'))"
                "   AND  (toitem_tohead_id=<? value(\"tohead_id\") ?>)"
                "   AND  (item_upccode=<? value(\"bc\") ?>))"
                " GROUP BY toitem_id, toitem_qty_ordered, toitem_qty_shipped; "
                "<? endif ?>"
                ;

  ParameterList findp;
  if (_order->isSO())
    findp.append("sohead_id", _order->id());
  else if (_order->isTO())
    findp.append("tohead_id", _order->id());
  findp.append("ordertype",   _order->type());
  findp.append("bc",          _bc->text());

  MetaSQLQuery findm(sql);
  issueBcFind = findm.toQuery(findp);

  if(!issueBcFind.first())
  {
    if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Barcode Information"),
                                  issueBcFind, __FILE__, __LINE__))
    {
      return;
    }
    XMessageBox::message(this, QMessageBox::Warning, tr("No Match Found"),
                         tr("<p>No Items on this Sales Order match the specified Barcode.") );
    _bc->clear();
    return;
  }

  int coitemid = issueBcFind.value("lineitem_id").toInt();
  while(issueBcFind.value("balance").toDouble() == 0 && issueBcFind.next())
    coitemid = issueBcFind.value("lineitem_id").toInt();

  ParameterList params;
  if (_order->isSO())
    params.append("soitem_id", coitemid);
  else if (_order->isTO())
    params.append("toitem_id", coitemid);

  if (_requireInventory->isChecked())
    params.append("requireInventory");
  params.append("qty", _bcQty->toDouble());
  params.append("issue");
  params.append("snooze");

  issueLineToShipping newdlg(this, "", true);
  if (newdlg.set(params) != NoError)
    return;
  sFillList();

  _bc->clear();

  // Check to see if the order is fully completed yet.
  // If so we can pop a quick message informing the user.
  // TODO: can we make this an orderitem select?
  sql = "<? if exists(\"sohead_id\") ?>"
        "SELECT coitem_id,"
        "       noNeg(coitem_qtyord - coitem_qtyshipped + coitem_qtyreturned) - COALESCE(SUM(shipitem_qty), 0) AS remaining"
        "  FROM coitem LEFT OUTER JOIN"
        "         ( shipitem JOIN shiphead"
        "           ON ( (shipitem_shiphead_id=shiphead_id)"
        "                 AND (NOT shiphead_shipped)"
        "                 AND (shiphead_order_type=<? value(\"ordertype\") ?>))"
        "         ) ON  (shipitem_orderitem_id=coitem_id)"
        " WHERE ( (coitem_status NOT IN ('C','X'))"
        "   AND   (coitem_cohead_id=<? value(\"sohead_id\") ?>) )"
        " GROUP BY coitem_id, coitem_qtyord, coitem_qtyshipped, coitem_qtyreturned "
        "HAVING ((noNeg(coitem_qtyord - coitem_qtyshipped + coitem_qtyreturned) - COALESCE(SUM(shipitem_qty),0)) > 0);"
        "<? elseif exists(\"tohead_id\") ?>"
        "SELECT toitem_id,"
        "       noNeg(toitem_qty_ordered - toitem_qty_shipped) - COALESCE(SUM(shipitem_qty), 0) AS remaining"
        "  FROM toitem LEFT OUTER JOIN"
        "         ( shipitem JOIN shiphead"
        "           ON ( (shipitem_shiphead_id=shiphead_id)"
        "                 AND (NOT shiphead_shipped)"
        "                 AND (shiphead_order_type=<? value(\"ordertype\") ?>))"
        "         ) ON  (shipitem_orderitem_id=toitem_id)"
        " WHERE ( (toitem_status NOT IN ('C','X'))"
        "   AND   (toitem_tohead_id=<? value(\"tohead_id\") ?>) )"
        " GROUP BY toitem_id, toitem_qty_ordered, toitem_qty_shipped "
        "HAVING ((noNeg(toitem_qty_ordered - toitem_qty_shipped) - COALESCE(SUM(shipitem_qty),0)) > 0);"
        "<? endif ?>"
        ;

  ParameterList fullp;
  if (_order->isSO())
    fullp.append("sohead_id", _order->id());
  else if (_order->isTO())
    fullp.append("tohead_id", _order->id());
  fullp.append("ordertype",   _order->type());
  fullp.append("bc",          _bc->text());

  MetaSQLQuery fullm(sql);
  issueBcFind = fullm.toQuery(fullp);

  // If there are no records then the order is complete
  if(!issueBcFind.first())
  {
    if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Barcode Information"),
                                  issueBcFind, __FILE__, __LINE__))
    {
      return;
    }
    XMessageBox::message( this, QMessageBox::Information, tr("Order Complete"),
                          tr("All items for this order have been issued to shipping.") );
  }
}

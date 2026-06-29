#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "clientmodel.h"
#include "walletmodel.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "transactionfilterproxy.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "util.h"   // fStopStaking, fTestNet

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QFrame>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QIcon>

using namespace boost;

#define DECORATION_SIZE 64
#define NUM_ITEMS 6

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(): QAbstractItemDelegate(), unit(BitcoinUnits::BTC)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
#if QT_VERSION < 0x050000
        if(qVariantCanConvert<QColor>(value))
#else
        if(value.canConvert<QColor>())
#endif
        {
            foreground = qvariant_cast<QColor>(value);
        }

        painter->setPen(foreground);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentBalanceWatchOnly(0),
    currentStake(0),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentTotBalance(-1),
    txdelegate(new TxViewDelegate()),
    filter(0)
{
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // On-page staking panel (built in code, inserted under the balance frame)
    createStakingPanel();

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::createStakingPanel()
{
    stakingFrame = new QFrame(this);
    stakingFrame->setFrameShape(QFrame::StyledPanel);
    stakingFrame->setFrameShadow(QFrame::Raised);

    QVBoxLayout *outer = new QVBoxLayout(stakingFrame);

    // Header: coin/staking icon + a gold "Staking" title (the theme accent IS the HBN gold).
    QHBoxLayout *header = new QHBoxLayout();
    QLabel *icon = new QLabel(stakingFrame);
    icon->setPixmap(QIcon(":/icons/staking_on").pixmap(20, 20));
    QLabel *title = new QLabel(tr("Staking"), stakingFrame);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.1);
    title->setFont(tf);
    title->setStyleSheet(QString("color:%1;").arg(palette().color(QPalette::Link).name()));
    header->addWidget(icon);
    header->addWidget(title);
    header->addStretch();
    outer->addLayout(header);

    QFormLayout *form = new QFormLayout();
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    labelStakingStatus = new QLabel(stakingFrame);
    labelStakeWeight   = new QLabel(stakingFrame);
    labelNetworkWeight = new QLabel(stakingFrame);
    labelNetworkShare  = new QLabel(stakingFrame);
    labelExpectedTime  = new QLabel(stakingFrame);
    QFont vf = labelStakeWeight->font();
    vf.setBold(true);
    labelStakeWeight->setFont(vf);
    labelNetworkWeight->setFont(vf);
    labelNetworkShare->setFont(vf);
    labelExpectedTime->setFont(vf);
    labelExpectedTime->setToolTip(tr("Estimated time until you have a 50% chance of producing a stake"));
    labelNetworkShare->setToolTip(tr("Your stake weight as a share of the whole network's weight"));

    form->addRow(new QLabel(tr("Status:"), stakingFrame), labelStakingStatus);
    form->addRow(new QLabel(tr("Your weight:"), stakingFrame), labelStakeWeight);
    form->addRow(new QLabel(tr("Network weight:"), stakingFrame), labelNetworkWeight);
    form->addRow(new QLabel(tr("Your share:"), stakingFrame), labelNetworkShare);
    form->addRow(new QLabel(tr("Expected time:"), stakingFrame), labelExpectedTime);
    outer->addLayout(form);

    // Insert just below the balance frame (index 0) in the left column.
    ui->verticalLayout_2->insertWidget(1, stakingFrame);

    // Periodic refresh -- weight/estimate drift even without a new block.
    stakingTimer = new QTimer(this);
    connect(stakingTimer, SIGNAL(timeout()), this, SLOT(updateStakingStats()));
    stakingTimer->start(5000);

    updateStakingStats();
}

void OverviewPage::updateStakingStats()
{
    if (!stakingFrame)
        return;
    if (!clientModel || !walletModel)
    {
        labelStakingStatus->setText(QString::fromUtf8("\xE2\x80\xA6"));   // ellipsis
        return;
    }

    uint64_t nMinWeight = 0, nMaxWeight = 0, nWeight = 0;
    walletModel->getStakeWeight(nMinWeight, nMaxWeight, nWeight);
    quint64 nNetworkWeight = (quint64)clientModel->getPosKernalPS();
    int nConn = clientModel->getNumConnections();

    QString status;
    bool fActive = false;
    if (nConn == 0)
        status = tr("Offline");
    else if (nConn < (fTestNet ? 0 : 3))
        status = tr("Acquiring nodes…");
    else if (clientModel->inInitialBlockDownload() ||
             clientModel->getNumBlocks() < clientModel->getNumBlocksOfPeers())
        status = tr("Syncing…");
    else if (walletModel->getEncryptionStatus() == WalletModel::Locked)
        status = tr("Locked");
    else if (fStopStaking)
        status = tr("Off");
    else if (!nWeight)
        status = tr("No mature coins");
    else { fActive = true; status = tr("Active"); }

    labelStakeWeight->setText(QString::number(nWeight));
    labelNetworkWeight->setText(QString::number(nNetworkWeight));

    double share = (nNetworkWeight > 0) ? (100.0 * (double)nWeight / (double)nNetworkWeight) : 0.0;
    labelNetworkShare->setText(QString::number(share, 'f', 3) + "%");

    if (fActive && nWeight > 0 && nNetworkWeight > 0)
    {
        // int64 math: avoids the int overflow the old status-bar estimate had for tiny weights.
        int64_t spacing = clientModel->getStakeTargetSpacing();
        int64_t eta = spacing * (int64_t)nNetworkWeight / (int64_t)nWeight;
        labelExpectedTime->setText(GUIUtil::formatDurationStr(eta));
    }
    else
        labelExpectedTime->setText(tr("n/a"));

    // Gold while actively staking, muted otherwise.
    QColor col = fActive ? palette().color(QPalette::Link)
                         : palette().color(QPalette::Disabled, QPalette::Text);
    labelStakingStatus->setText(status);
    labelStakingStatus->setStyleSheet(QString("color:%1; font-weight:bold;").arg(col.name()));
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}
void OverviewPage::setBalance(qint64 balance, qint64 watchOnly, qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentBalanceWatchOnly = watchOnly;
    currentStake = stake;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance));
    ui->labelBalanceWatchOnly->setText(BitcoinUnits::formatWithUnit(unit, watchOnly));
    ui->labelStake->setText(BitcoinUnits::formatWithUnit(unit, stake));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    ui->labelImmature->setVisible(showImmature);
    ui->labelImmatureText->setVisible(showImmature);

    // only show watch-only balance if it's non-zero, so as not to complicate things
    // for users
    bool showWatchOnly = watchOnly != 0;
    ui->labelBalanceWatchOnly->setVisible(showWatchOnly);
    ui->labelBalanceWatchOnlyText->setVisible(showWatchOnly);
}

void OverviewPage::setTotBalance(qint64 totBalance)
{
   currentTotBalance = totBalance;
   int unit = walletModel->getOptionsModel()->getDisplayUnit();
   ui->labelTotBalance->setText(BitcoinUnits::formatWithUnit(unit, totBalance));
}

void OverviewPage::setNumTransactions(int count)
{
    ui->labelNumTransactions->setText(QLocale::system().toString(count));
}
void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());

        // Refresh the staking panel as the chain advances.
        connect(model, SIGNAL(numBlocksChanged(int,int)), this, SLOT(updateStakingStats()));
        updateStakingStats();
    }
}
void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getBalanceWatchOnly(), model->getStake(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64, qint64, qint64)));

        setNumTransactions(model->getNumTransactions());
        connect(model, SIGNAL(numTransactionsChanged(int)), this, SLOT(setNumTransactions(int)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("HBN")
    updateDisplayUnit();

    updateStakingStats();
}
void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
        {
            setBalance(currentBalance, currentBalanceWatchOnly, walletModel->getStake(), currentUnconfirmedBalance, currentImmatureBalance);
            setTotBalance(currentTotBalance);
        }

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

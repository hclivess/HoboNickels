#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>

namespace Ui {
    class OverviewPage;
}
class ClientModel;
class WalletModel;
class TxViewDelegate;
class TransactionFilterProxy;

QT_BEGIN_NAMESPACE
class QModelIndex;
class QFrame;
class QLabel;
class QTimer;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(QWidget *parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);

public slots:
    void setBalance(qint64 balance, qint64 watchOnly, qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance);
    void setTotBalance(qint64 totBalance);
    void setNumTransactions(int count);

signals:
    void transactionClicked(const QModelIndex &index);

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    qint64 currentBalance;
    qint64 currentBalanceWatchOnly;
    qint64 currentStake;
    qint64 currentUnconfirmedBalance;
    qint64 currentImmatureBalance;
    qint64 currentTotBalance;

    TxViewDelegate *txdelegate;
    TransactionFilterProxy *filter;

    // On-page staking panel (gold-accented), built in code to avoid fragile .ui edits.
    QFrame *stakingFrame;
    QLabel *labelStakingStatus;
    QLabel *labelStakeWeight;
    QLabel *labelNetworkWeight;
    QLabel *labelNetworkShare;
    QLabel *labelExpectedTime;
    QTimer *stakingTimer;
    void createStakingPanel();

private slots:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);
    void updateStakingStats();
};

#endif // OVERVIEWPAGE_H

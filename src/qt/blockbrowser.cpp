#include "blockbrowser.h"
#include "ui_blockbrowser.h"
#include "main.h"
#include "base58.h"
#include "clientmodel.h"
#include "txdb.h"
#include "dialogwindowflags.h"

double GetPoSKernelPS(const CBlockIndex* blockindex);
double GetDifficulty(const CBlockIndex* blockindex);
double GetPoWMHashPS(const CBlockIndex* blockindex);

using namespace std;

const CBlockIndex* getBlockIndex(qint64 height)
{
    std::string hex = getBlockHash(height);
    uint256 hash(hex);
    return mapBlockIndex[hash];
}

std::string getBlockHash(qint64 Height)
{
    if(Height > pindexBest->nHeight) { return ""; }
    if(Height < 0) { return ""; }
    qint64 desiredheight;
    desiredheight = Height;
    if (desiredheight < 0 || desiredheight > nBestHeight)
        return "";   // (was 'return 0' -> constructs std::string from null = UB)

    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > desiredheight)
        pblockindex = pblockindex->pprev;
    return  pblockindex->GetBlockHash().GetHex(); // pblockindex->phashBlock->GetHex();
}

qint64 getBlockTime(qint64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->nTime;
}

std::string getBlockMerkle(qint64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return "";   // (was 'return 0' -> std::string from null = UB)

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->hashMerkleRoot.ToString();//.substr(0,10).c_str();
}

qint64 getBlocknBits(qint64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->nBits;
}

qint64 getBlockNonce(qint64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return pblockindex->nNonce;
}

double getTxTotalValue(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        return 0;

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;

    double value = 0;
    double buffer = 0;
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        buffer = value + convertCoins(txout.nValue);
        value = buffer;
    }

    return value;
}

double getMoneySupply(qint64 Height)
{
    std::string strHash = getBlockHash(Height);
    uint256 hash(strHash);

    if (mapBlockIndex.count(hash) == 0)
        return 0;

    CBlockIndex* pblockindex = mapBlockIndex[hash];
    return convertCoins(pblockindex->nMoneySupply);
}

double convertCoins(qint64 amount)
{
    // Tranz needs to use options model.
    return (double)amount / (double)COIN;
}

std::string getOutputs(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        return "N/A";

    std::string str = "";
    for (unsigned int i = (tx.IsCoinStake() ? 1 : 0); i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];

        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address) )
            address = CNoDestination();

        double buffer = convertCoins(txout.nValue);
        std::string amount = boost::to_string(buffer);
        str.append(CBitcoinAddress(address).ToString());
        str.append(": ");
        str.append(amount);
        str.append(" HBN");
        str.append("\n");
    }

    return str;
}

std::string getInputs(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(hash, tx, hashBlock))
        return "N/A";

    std::string str = "";
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        uint256 hash;
        const CTxIn& vin = tx.vin[i];

        hash.SetHex(vin.prevout.hash.ToString());
        CTransaction wtxPrev;
        uint256 hashBlock = 0;
        if (!GetTransaction(hash, wtxPrev, hashBlock))
             return "N/A";

        CTxDestination address;
        if (!ExtractDestination(wtxPrev.vout[vin.prevout.n].scriptPubKey, address) )
            address = CNoDestination();

        double buffer = convertCoins(wtxPrev.vout[vin.prevout.n].nValue);
        std::string amount = boost::to_string(buffer);
        str.append(CBitcoinAddress(address).ToString());
        str.append(": ");
        str.append(amount);
        str.append(" HBN");
        str.append("\n");
    }

    return str;
}

double BlockBrowser::getTxFees(std::string txid)
{
    uint256 hash;
    hash.SetHex(txid);

    CTransaction tx;
    uint256 hashBlock = 0;
    CTxDB txdb("r");

    if (!GetTransaction(hash, tx, hashBlock))
        return convertCoins(MIN_TX_FEE);

    MapPrevTx mapInputs;
    map<uint256, CTxIndex> mapUnused;
    bool fInvalid;

    if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        return convertCoins(MIN_TX_FEE);

    qint64 nTxFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();

    if(tx.IsCoinStake() || tx.IsCoinBase()) {
        ui->feesLabel->setText(QString("Reward"));
        nTxFees *= -1;
    }
    else
        ui->feesLabel->setText(QString("Fees"));

    return convertCoins(nTxFees);
}


BlockBrowser::BlockBrowser(QWidget *parent) :
    QDialog(parent, (DIALOGWINDOWHINTS|Qt::WindowMinMaxButtonsHint)),
    ui(new Ui::BlockBrowser)
{
    ui->setupUi(this);

    setBaseSize(850, 500);

#if QT_VERSION >= 0x040700
    /* Do not move this to the XML file, Qt before 4.7 will choke on it */
    ui->txBox->setPlaceholderText(tr("Transaction ID"));
#endif

    connect(ui->blockButton, SIGNAL(pressed()), this, SLOT(blockClicked()));
    connect(ui->txButton, SIGNAL(pressed()), this, SLOT(txClicked()));
    connect(ui->closeButton, SIGNAL(pressed()), this, SLOT(close()));
    // Live navigation: the height spin-box arrows (and typed values) update the view, so
    // they act as Prev/Next without extra buttons. Cheap now that a view is a single walk.
    connect(ui->heightBox, SIGNAL(valueChanged(int)), this, SLOT(blockClicked()));
}

void BlockBrowser::updateExplorer(bool block)
{
    if(block)
    {
        if (!pindexBest)
            return;
        qint64 height = ui->heightBox->value();
        if (height > pindexBest->nHeight) { height = pindexBest->nHeight; ui->heightBox->setValue(height); }
        if (height < 0)                   { height = 0;                   ui->heightBox->setValue(0); }

        // Resolve the block index ONCE (a single tip->height pprev walk), then read every
        // field directly off it. The old code called getBlockHash(height) -- a full chain
        // walk -- separately for hash/merkle/bits/nonce/time/supply, i.e. ~7 walks per
        // view, which was painfully slow on a long chain. Now it's one.
        const CBlockIndex* pindex = getBlockIndex(height);
        if (!pindex)
            return;

        ui->heightLabelBE1->setText(QString::number(height));
        ui->hashBox->setText(QString::fromStdString(pindex->GetBlockHash().GetHex()));
        ui->merkleBox->setText(QString::fromStdString(pindex->hashMerkleRoot.ToString()));
        ui->bitsBox->setText(QString::number(pindex->nBits));
        ui->nonceBox->setText(QString::number(pindex->nNonce));
        ui->timeBox->setText(QString::fromStdString(DateTimeStrFormat(pindex->nTime)));
        ui->diffBox->setText(QString::number(GetDifficulty(pindex), 'f', 6));
        if (pindex->IsProofOfStake()) {
            ui->hashRateLabel->setText(tr("Block Network Stake Weight:"));
            ui->diffLabel->setText(tr("PoS Block Difficulty:"));
            ui->hashRateBox->setText(QString::number(GetPoSKernelPS(pindex), 'f', 3) + " ");
        }
        else {
            ui->hashRateLabel->setText(tr("Block Hash Rate:"));
            ui->diffLabel->setText(tr("PoW Block Difficulty:"));
            ui->hashRateBox->setText(QString::number(GetPoWMHashPS(pindex), 'f', 3) + " MH/s");
        }
        ui->moneySupplyBox->setText(QString::number(convertCoins(pindex->nMoneySupply), 'f', 6) + " HBN");
    }
    else {
        std::string txid = ui->txBox->text().toUtf8().constData();

        ui->valueBox->setText(QString::number(getTxTotalValue(txid), 'f', 6) + " HBN");
        ui->txID->setText(QString::fromUtf8(txid.c_str()));
        ui->outputBox->setText(QString::fromUtf8(getOutputs(txid).c_str()));
        ui->inputBox->setText(QString::fromUtf8(getInputs(txid).c_str()));
        ui->feesBox->setText(QString::number(getTxFees(txid), 'f', 6) + " HBN");
    }
}

void BlockBrowser::setTransactionId(const QString &transactionId)
{
    ui->txBox->setText(transactionId);
    ui->txBox->setFocus();
    updateExplorer(false);

    uint256 hash;
    hash.SetHex(transactionId.toStdString());

    CTransaction tx;
    uint256 hashBlock = 0;
    if (GetTransaction(hash, tx, hashBlock))
    {
        CBlockIndex* pblockindex = mapBlockIndex[hashBlock];
        if (!pblockindex)
            ui->heightBox->setValue(nBestHeight);
        else
            ui->heightBox->setValue(pblockindex->nHeight);
        updateExplorer(true);
    }
}

void BlockBrowser::txClicked()
{
    updateExplorer(false);
}

void BlockBrowser::blockClicked()
{
    updateExplorer(true);
}

void BlockBrowser::setModel(ClientModel *model)
{
    this->model = model;
}

BlockBrowser::~BlockBrowser()
{
    delete ui;
}

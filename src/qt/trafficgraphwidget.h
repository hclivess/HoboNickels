#ifndef TRAFFICGRAPHWIDGET_H
#define TRAFFICGRAPHWIDGET_H

#include <QWidget>
#include <QQueue>
#include <QPoint>

class ClientModel;

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QMouseEvent;
class QPainterPath;
class QTimer;
QT_END_NAMESPACE

class TrafficGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrafficGraphWidget(QWidget *parent = 0);
    void setClientModel(ClientModel *model);
    int getGraphRangeMins() const;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

public slots:
    void updateRates();
    void setGraphRangeMins(int mins);
    void clear();

private:
    // Number of stored samples that the current timeframe should display.
    int visibleSamples() const;
    // Peak value (KB/s) across the currently visible window, for auto-scaling.
    void rescaleMax();
    // Format a KB/s rate with an adaptive B/s, KB/s or MB/s unit.
    QString formatRate(float kbps, int decimals = 1) const;

    QTimer *timer;
    float fMax;
    int nMins;
    QQueue<float> vSamplesIn;
    QQueue<float> vSamplesOut;
    quint64 nLastBytesIn;
    quint64 nLastBytesOut;
    ClientModel *clientModel;

    // Interactive crosshair state.
    QPoint hoverPos;
    bool hover;
};

#endif // TRAFFICGRAPHWIDGET_H

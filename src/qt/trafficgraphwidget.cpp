#include "trafficgraphwidget.h"
#include "clientmodel.h"

#include <QPainter>
#include <QPainterPath> // Qt5 split QPainterPath out of <QtGui>
#include <QColor>
#include <QTimer>

#include <cmath>

#define DESIRED_SAMPLES         800

#define XMARGIN                 10
#define YMARGIN                 10

TrafficGraphWidget::TrafficGraphWidget(QWidget *parent) :
    QWidget(parent),
    timer(0),
    fMax(0.0f),
    nMins(0),
    vSamplesIn(),
    vSamplesOut(),
    nLastBytesIn(0),
    nLastBytesOut(0),
    clientModel(0)
{
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), SLOT(updateRates()));
}

void TrafficGraphWidget::setClientModel(ClientModel *model)
{
    clientModel = model;
    if(model) {
        nLastBytesIn = model->getTotalBytesRecv();
        nLastBytesOut = model->getTotalBytesSent();
    }
}

int TrafficGraphWidget::getGraphRangeMins() const
{
    return nMins;
}

void TrafficGraphWidget::paintPath(QPainterPath &path, QQueue<float> &samples)
{
    int h = height() - YMARGIN * 2, w = width() - XMARGIN * 2;
    int sampleCount = samples.size(), x = XMARGIN + w, y;
    if(sampleCount > 0) {
        path.moveTo(x, YMARGIN + h);
        for(int i = 0; i < sampleCount; ++i) {
            x = XMARGIN + w - w * i / DESIRED_SAMPLES;
            y = YMARGIN + h - (int)(h * samples.at(i) / fMax);
            path.lineTo(x, y);
        }
        path.lineTo(x, YMARGIN + h);
    }
}

void TrafficGraphWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);            // modern: smooth lines (Core does this)
    painter.fillRect(rect(), palette().color(QPalette::Base)); // theme-aware bg (was hardcoded black)

    if(fMax <= 0.0f) return;

    QColor axisCol = palette().color(QPalette::Mid);          // theme-aware axis (was hardcoded gray)
    int h = height() - YMARGIN * 2;
    painter.setPen(axisCol);
    painter.drawLine(XMARGIN, YMARGIN + h, width() - XMARGIN, YMARGIN + h);

    // decide what order of magnitude we are
    int base = floor(log10(fMax));
    float val = pow(10.0f, base);

    const QString units = tr("KB/s");
    // draw lines
    painter.setPen(axisCol);
    painter.drawText(XMARGIN, YMARGIN + h - h * val / fMax, QString("%1 %2").arg(val).arg(units));
    for(float y = val; y < fMax; y += val) {
        int yy = YMARGIN + h - h * y / fMax;
        painter.drawLine(XMARGIN, yy, width() - XMARGIN, yy);
    }
    // if we drew 3 or fewer lines, break them up at the next lower order of magnitude
    if(fMax / val <= 3.0f) {
        axisCol = axisCol.darker();
        val = pow(10.0f, base - 1);
        painter.setPen(axisCol);
        painter.drawText(XMARGIN, YMARGIN + h - h * val / fMax, QString("%1 %2").arg(val).arg(units));
        int count = 1;
        for(float y = val; y < fMax; y += val, count++) {
            // don't overwrite lines drawn above
            if(count % 10 == 0)
                continue;
            int yy = YMARGIN + h - h * y / fMax;
            painter.drawLine(XMARGIN, yy, width() - XMARGIN, yy);
        }
    }

    // Toned green/red that read on both light and dark backgrounds.
    const QColor inCol(0, 160, 0), outCol(200, 40, 40);
    if(!vSamplesIn.empty()) {
        QPainterPath p;
        paintPath(p, vSamplesIn);
        painter.fillPath(p, QColor(inCol.red(), inCol.green(), inCol.blue(), 96));
        painter.setPen(inCol);
        painter.drawPath(p);
    }
    if(!vSamplesOut.empty()) {
        QPainterPath p;
        paintPath(p, vSamplesOut);
        painter.fillPath(p, QColor(outCol.red(), outCol.green(), outCol.blue(), 96));
        painter.setPen(outCol);
        painter.drawPath(p);
    }

    // Legend with live in/out rates (top-left). front() is the newest sample.
    float inRate  = vSamplesIn.empty()  ? 0.0f : vSamplesIn.front();
    float outRate = vSamplesOut.empty() ? 0.0f : vSamplesOut.front();
    int sw = 10, gap = 6, lx = XMARGIN + 4, ly = YMARGIN + 4;
    int rowh = painter.fontMetrics().height() + 3;
    painter.fillRect(lx, ly, sw, sw, inCol);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(lx + sw + gap, ly + sw, tr("In: %1 KB/s").arg(inRate, 0, 'f', 1));
    painter.fillRect(lx, ly + rowh, sw, sw, outCol);
    painter.drawText(lx + sw + gap, ly + rowh + sw, tr("Out: %1 KB/s").arg(outRate, 0, 'f', 1));
}

void TrafficGraphWidget::updateRates()
{
    if(!clientModel) return;

    quint64 bytesIn = clientModel->getTotalBytesRecv(),
            bytesOut = clientModel->getTotalBytesSent();
    float inRate = (bytesIn - nLastBytesIn) / 1024.0f * 1000 / timer->interval();
    float outRate = (bytesOut - nLastBytesOut) / 1024.0f * 1000 / timer->interval();
    vSamplesIn.push_front(inRate);
    vSamplesOut.push_front(outRate);
    nLastBytesIn = bytesIn;
    nLastBytesOut = bytesOut;

    while(vSamplesIn.size() > DESIRED_SAMPLES) {
        vSamplesIn.pop_back();
    }
    while(vSamplesOut.size() > DESIRED_SAMPLES) {
        vSamplesOut.pop_back();
    }

    float tmax = 0.0f;
    foreach(float f, vSamplesIn) {
        if(f > tmax) tmax = f;
    }
    foreach(float f, vSamplesOut) {
        if(f > tmax) tmax = f;
    }
    fMax = tmax;
    update();
}

void TrafficGraphWidget::setGraphRangeMins(int mins)
{
    nMins = mins;
    int msecsPerSample = nMins * 60 * 1000 / DESIRED_SAMPLES;
    timer->stop();
    timer->setInterval(msecsPerSample);

    clear();
}

void TrafficGraphWidget::clear()
{
    timer->stop();

    vSamplesOut.clear();
    vSamplesIn.clear();
    fMax = 0.0f;

    if(clientModel) {
        nLastBytesIn = clientModel->getTotalBytesRecv();
        nLastBytesOut = clientModel->getTotalBytesSent();
    }
    timer->start();
}
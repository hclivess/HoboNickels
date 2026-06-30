#include "trafficgraphwidget.h"
#include "clientmodel.h"

#include <QPainter>
#include <QPainterPath> // Qt5 split QPainterPath out of <QtGui>
#include <QLinearGradient>
#include <QColor>
#include <QPen>
#include <QTimer>
#include <QMouseEvent>
#include <QVector>
#include <QPointF>

#include <algorithm>
#include <cmath>

// Sampling is decoupled from the display range: we always sample at a fixed
// cadence and keep a rolling buffer that covers the widest selectable range.
// Changing the timeframe then just re-windows the existing data instead of
// throwing it away, so the graph never blanks or stalls on a range change.
#define SAMPLE_INTERVAL_MS      1000          // one sample per second
#define MAX_RANGE_MINS          1440          // 24h, matches the slider maximum
#define MAX_SAMPLES             (MAX_RANGE_MINS * 60 * 1000 / SAMPLE_INTERVAL_MS)

#define XMARGIN                 14
#define YMARGIN                 12

namespace {

// Pick a "nice" 1/2/5 x 10^n grid step so we end up with ~target gridlines.
float niceStep(float range, int target)
{
    if(range <= 0.0f || target < 1) return 1.0f;
    float raw = range / target;
    float mag = std::pow(10.0f, std::floor(std::log10(raw)));
    float norm = raw / mag;
    float s;
    if(norm < 1.5f)      s = 1.0f;
    else if(norm < 3.5f) s = 2.0f;
    else if(norm < 7.5f) s = 5.0f;
    else                 s = 10.0f;
    return s * mag;
}

// Peak-preserving downsample of the visible window into screen-space points,
// ordered left (oldest) -> right (newest). samples are stored newest-first.
QVector<QPointF> collectPoints(const QQueue<float> &samples, const QRectF &plot,
                               int vis, float fMax)
{
    QVector<QPointF> pts;
    if(vis < 1 || fMax <= 0.0f) return pts;
    vis = std::min(vis, samples.size());
    if(vis < 1) return pts;

    int nBuckets = std::max(1, std::min<int>(qRound(plot.width()), vis));
    pts.reserve(nBuckets);
    const double span = (vis > 1) ? double(vis - 1) : 1.0;
    for(int b = 0; b < nBuckets; ++b) {
        double fL = double(b) / nBuckets;
        double fR = double(b + 1) / nBuckets;
        // left edge -> oldest (largest index), right edge -> newest (index 0)
        int iA = int(std::lround((1.0 - fR) * span));
        int iB = int(std::lround((1.0 - fL) * span));
        if(iA > iB) std::swap(iA, iB);
        iA = std::max(0, std::min(iA, vis - 1));
        iB = std::max(0, std::min(iB, vis - 1));
        float peak = 0.0f;
        for(int i = iA; i <= iB; ++i)
            peak = std::max(peak, samples.at(i));
        double fc = (fL + fR) * 0.5;
        double x = plot.left() + plot.width() * fc;
        double y = plot.bottom() - plot.height() * (peak / fMax);
        pts.append(QPointF(x, y));
    }
    return pts;
}

// Smooth the points with a Catmull-Rom spline (converted to cubic Beziers).
// Control points are clamped into the plot rect so the area fill can never
// overshoot below the baseline or above the top edge.
QPainterPath buildCurve(const QVector<QPointF> &p, bool close,
                        double topY, double baselineY)
{
    QPainterPath path;
    const int n = p.size();
    if(n == 0) return path;
    path.moveTo(p.first());
    if(n == 1) return path;
    for(int i = 0; i < n - 1; ++i) {
        const QPointF &p0 = p.at(std::max(0, i - 1));
        const QPointF &p1 = p.at(i);
        const QPointF &p2 = p.at(i + 1);
        const QPointF &p3 = p.at(std::min(n - 1, i + 2));
        QPointF c1 = p1 + (p2 - p0) / 6.0;
        QPointF c2 = p2 - (p3 - p1) / 6.0;
        c1.setY(qBound(topY, c1.y(), baselineY));
        c2.setY(qBound(topY, c2.y(), baselineY));
        path.cubicTo(c1, c2, p2);
    }
    if(close) {
        path.lineTo(QPointF(p.last().x(), baselineY));
        path.lineTo(QPointF(p.first().x(), baselineY));
        path.closeSubpath();
    }
    return path;
}

} // namespace

TrafficGraphWidget::TrafficGraphWidget(QWidget *parent) :
    QWidget(parent),
    timer(0),
    fMax(0.0f),
    nMins(0),
    vSamplesIn(),
    vSamplesOut(),
    nLastBytesIn(0),
    nLastBytesOut(0),
    clientModel(0),
    hoverPos(),
    hover(false)
{
    timer = new QTimer(this);
    timer->setInterval(SAMPLE_INTERVAL_MS);
    connect(timer, SIGNAL(timeout()), SLOT(updateRates()));
    setMouseTracking(true);
}

void TrafficGraphWidget::setClientModel(ClientModel *model)
{
    clientModel = model;
    if(model) {
        nLastBytesIn = model->getTotalBytesRecv();
        nLastBytesOut = model->getTotalBytesSent();
        timer->start();
    }
}

int TrafficGraphWidget::getGraphRangeMins() const
{
    return nMins;
}

int TrafficGraphWidget::visibleSamples() const
{
    int want = std::max(1, nMins) * 60 * 1000 / SAMPLE_INTERVAL_MS;
    return std::min(want, vSamplesIn.size());
}

void TrafficGraphWidget::rescaleMax()
{
    int vis = visibleSamples();
    float m = 0.0f;
    for(int i = 0; i < vis && i < vSamplesIn.size(); ++i)
        m = std::max(m, vSamplesIn.at(i));
    for(int i = 0; i < vis && i < vSamplesOut.size(); ++i)
        m = std::max(m, vSamplesOut.at(i));
    fMax = m;
}

QString TrafficGraphWidget::formatRate(float kbps, int decimals) const
{
    if(kbps < 1.0f)
        return QString("%1 B/s").arg(qRound(kbps * 1024.0f));
    if(kbps < 1024.0f)
        return QString("%1 KB/s").arg(kbps, 0, 'f', decimals);
    return QString("%1 MB/s").arg(kbps / 1024.0f, 0, 'f', 2);
}

void TrafficGraphWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base)); // theme-aware bg

    QRectF plot(XMARGIN, YMARGIN, width() - 2 * XMARGIN, height() - 2 * YMARGIN);
    if(plot.width() < 4.0 || plot.height() < 4.0) return;

    const QColor textCol = palette().color(QPalette::Text);
    QColor gridCol = palette().color(QPalette::Mid); gridCol.setAlpha(60);
    QColor baseLineCol = palette().color(QPalette::Mid); baseLineCol.setAlpha(160);

    const int vis = visibleSamples();

    // Baseline.
    painter.setPen(QPen(baseLineCol, 1.0));
    painter.drawLine(QPointF(plot.left(), plot.bottom()),
                     QPointF(plot.right(), plot.bottom()));

    // Horizontal grid + adaptive-unit labels. Scope the smaller axis font to this
    // block so it can't leak into the legend/tooltip drawn later in the same paint.
    if(fMax > 0.0f) {
        painter.save();
        QFont f = painter.font();
        f.setPointSizeF(std::max(7.0, (f.pointSizeF() > 0 ? f.pointSizeF() : 9.0) - 1.0));
        painter.setFont(f);
        float step = niceStep(fMax, 4);
        QColor lblCol = textCol; lblCol.setAlpha(150);
        int guard = 0;
        for(float g = step; g <= fMax * 1.0001f && guard < 32; g += step, ++guard) {
            qreal y = plot.bottom() - plot.height() * (g / fMax);
            painter.setPen(QPen(gridCol, 1.0));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(lblCol);
            int dec = (g < 1.0f) ? 2 : (g < 10.0f ? 1 : 0);
            painter.drawText(QPointF(plot.left() + 4.0, y - 2.0), formatRate(g, dec));
        }
        painter.restore();
    }

    // Modern flat palette that reads on both light and dark themes.
    const QColor inCol(46, 204, 113);   // emerald (received)
    const QColor outCol(231, 76, 60);   // alizarin (sent)

    auto drawSeries = [&](const QQueue<float> &samples, const QColor &col) {
        if(fMax <= 0.0f) return;
        QVector<QPointF> pts = collectPoints(samples, plot, vis, fMax);
        if(pts.size() < 2) return;
        QPainterPath area = buildCurve(pts, true, plot.top(), plot.bottom());
        QLinearGradient grad(QPointF(0, plot.top()), QPointF(0, plot.bottom()));
        QColor top = col; top.setAlpha(130);
        QColor bot = col; bot.setAlpha(12);
        grad.setColorAt(0.0, top);
        grad.setColorAt(1.0, bot);
        painter.fillPath(area, grad);
        QPainterPath line = buildCurve(pts, false, plot.top(), plot.bottom());
        painter.setPen(QPen(col, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(line);
    };

    drawSeries(vSamplesIn, inCol);
    drawSeries(vSamplesOut, outCol);

    // Interactive crosshair + value readout on hover.
    if(hover && vis >= 1 && fMax > 0.0f &&
       hoverPos.x() >= plot.left() && hoverPos.x() <= plot.right() &&
       hoverPos.y() >= plot.top()  && hoverPos.y() <= plot.bottom())
    {
        double x = std::min<double>(std::max<double>(hoverPos.x(), plot.left()), plot.right());
        double f = (plot.width() > 0) ? (x - plot.left()) / plot.width() : 0.0;
        int idx = int(std::lround((1.0 - f) * std::max(0, vis - 1)));
        idx = std::max(0, std::min(idx, vis - 1));

        QColor cross = textCol; cross.setAlpha(90);
        painter.setPen(QPen(cross, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

        float inV  = idx < vSamplesIn.size()  ? vSamplesIn.at(idx)  : 0.0f;
        float outV = idx < vSamplesOut.size() ? vSamplesOut.at(idx) : 0.0f;
        double yIn  = plot.bottom() - plot.height() * (inV  / fMax);
        double yOut = plot.bottom() - plot.height() * (outV / fMax);
        painter.setPen(Qt::NoPen);
        painter.setBrush(inCol);  painter.drawEllipse(QPointF(x, yIn),  3.0, 3.0);
        painter.setBrush(outCol); painter.drawEllipse(QPointF(x, yOut), 3.0, 3.0);

        int secs = idx; // SAMPLE_INTERVAL_MS == 1000 -> one sample per second
        QString when = (secs == 0) ? tr("now")
                     : (secs < 60) ? tr("%1s ago").arg(secs)
                                   : tr("%1m %2s ago").arg(secs / 60).arg(secs % 60);
        QStringList lines;
        lines << when
              << tr("In  %1").arg(formatRate(inV))
              << tr("Out %1").arg(formatRate(outV));
        QFontMetrics fm(painter.font());
        int tw = 0;
        for(const QString &l : lines) tw = std::max(tw, fm.horizontalAdvance(l));
        int th = fm.height() * lines.size() + 8;
        tw += 12;
        double bx = (x + 14 + tw < plot.right()) ? x + 14 : x - 14 - tw;
        double by = std::max<double>(plot.top(), hoverPos.y() - th - 8);
        QColor box = palette().color(QPalette::ToolTipBase); box.setAlpha(235);
        painter.setBrush(box);
        painter.setPen(QPen(gridCol, 1.0));
        painter.drawRoundedRect(QRectF(bx, by, tw, th), 5.0, 5.0);
        painter.setPen(palette().color(QPalette::ToolTipText));
        for(int i = 0; i < lines.size(); ++i)
            painter.drawText(QPointF(bx + 6, by + 4 + fm.ascent() + i * fm.height()), lines.at(i));
    }

    // Legend pill (top-left) with live in/out rates. front() is the newest sample.
    {
        float inRate  = vSamplesIn.empty()  ? 0.0f : vSamplesIn.front();
        float outRate = vSamplesOut.empty() ? 0.0f : vSamplesOut.front();
        QStringList rows;
        rows << tr("In  %1").arg(formatRate(inRate))
             << tr("Out %1").arg(formatRate(outRate));
        QFontMetrics fm(painter.font());
        int rw = 0;
        for(const QString &r : rows) rw = std::max(rw, fm.horizontalAdvance(r));
        int dot = 9, gap = 7, pad = 7;
        int rowh = fm.height() + 3;
        QRectF pill(plot.left() + 6, plot.top() + 6,
                    pad * 2 + dot + gap + rw, pad * 2 + rowh * rows.size() - 3);
        QColor pillBg = palette().color(QPalette::Base); pillBg.setAlpha(180);
        painter.setPen(QPen(gridCol, 1.0));
        painter.setBrush(pillBg);
        painter.drawRoundedRect(pill, 6.0, 6.0);
        const QColor dotCols[2] = { inCol, outCol };
        for(int i = 0; i < rows.size(); ++i) {
            double ry = pill.top() + pad + i * rowh;
            painter.setPen(Qt::NoPen);
            painter.setBrush(dotCols[i]);
            painter.drawRoundedRect(QRectF(pill.left() + pad, ry + 1, dot, dot), 2.0, 2.0);
            painter.setPen(textCol);
            painter.drawText(QPointF(pill.left() + pad + dot + gap, ry + fm.ascent()), rows.at(i));
        }
    }
}

void TrafficGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    hoverPos = event->pos();
    hover = true;
    update();
}

void TrafficGraphWidget::leaveEvent(QEvent *)
{
    hover = false;
    update();
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

    while(vSamplesIn.size() > MAX_SAMPLES) vSamplesIn.pop_back();
    while(vSamplesOut.size() > MAX_SAMPLES) vSamplesOut.pop_back();

    rescaleMax();
    update();
}

void TrafficGraphWidget::setGraphRangeMins(int mins)
{
    // Non-destructive: just re-window the existing samples. The buffer already
    // holds up to MAX_RANGE_MINS of history, so widening or narrowing the range
    // is instant and never clears the graph.
    nMins = std::max(1, std::min(mins, MAX_RANGE_MINS));
    if(clientModel && !timer->isActive())
        timer->start();
    rescaleMax();
    update();
}

void TrafficGraphWidget::clear()
{
    vSamplesOut.clear();
    vSamplesIn.clear();
    fMax = 0.0f;

    if(clientModel) {
        nLastBytesIn = clientModel->getTotalBytesRecv();
        nLastBytesOut = clientModel->getTotalBytesSent();
        if(!timer->isActive()) timer->start();
    }
    update();
}

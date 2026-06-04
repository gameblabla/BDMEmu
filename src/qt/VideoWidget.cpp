#include "VideoWidget.h"

#include <QMouseEvent>
#include <QPainter>

VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent), m_image((int)BDM_LCD_WIDTH, (int)BDM_LCD_HEIGHT, QImage::Format_ARGB32), m_integerScaling(true) {
    setMinimumSize((int)BDM_LCD_WIDTH * 2, (int)BDM_LCD_HEIGHT * 2);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_image.fill(Qt::white);
}

void VideoWidget::setIntegerScaling(bool enabled) {
    m_integerScaling = enabled;
    update();
}

void VideoWidget::setFrame(const bdm_video_t *video) {
    size_t w = 0, h = 0;
    const uint32_t *fb = bdm_video_framebuffer(video, &w, &h);
    if (!fb || w != BDM_LCD_WIDTH || h != BDM_LCD_HEIGHT) return;
    for (int y = 0; y < (int)BDM_LCD_HEIGHT; ++y) {
        memcpy(m_image.scanLine(y), fb + (size_t)y * BDM_LCD_WIDTH, BDM_LCD_WIDTH * sizeof(uint32_t));
    }
    update();
}

QSize VideoWidget::sizeHint() const { return QSize((int)BDM_LCD_WIDTH * 4, (int)BDM_LCD_HEIGHT * 4); }

QRect VideoWidget::imageRect() const {
    QRect r = rect();
    if (!m_integerScaling) return r;
    int sx = r.width() / (int)BDM_LCD_WIDTH;
    int sy = r.height() / (int)BDM_LCD_HEIGHT;
    int s = qMax(1, qMin(sx, sy));
    QSize sz((int)BDM_LCD_WIDTH * s, (int)BDM_LCD_HEIGHT * s);
    return QRect(QPoint((r.width() - sz.width()) / 2, (r.height() - sz.height()) / 2), sz);
}

QPointF VideoWidget::toLcdF(const QPointF &p) const {
    QRect dst = imageRect();
    if (dst.width() <= 0 || dst.height() <= 0) return QPointF(0.0, 0.0);
    double x = ((p.x() - (double)dst.left()) * (double)BDM_LCD_WIDTH) / (double)dst.width();
    double y = ((p.y() - (double)dst.top()) * (double)BDM_LCD_HEIGHT) / (double)dst.height();
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;
    if (x >= (double)BDM_LCD_WIDTH) x = (double)BDM_LCD_WIDTH - (1.0 / 65536.0);
    if (y >= (double)BDM_LCD_HEIGHT) y = (double)BDM_LCD_HEIGHT - (1.0 / 65536.0);
    return QPointF(x, y);
}

void VideoWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(imageRect(), m_image);
}

void VideoWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF q = toLcdF(event->position());
        emit penDown((float)q.x(), (float)q.y());
    }
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        QPointF q = toLcdF(event->position());
        emit penMove((float)q.x(), (float)q.y());
    }
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF q = toLcdF(event->position());
        emit penUp((float)q.x(), (float)q.y());
    }
}

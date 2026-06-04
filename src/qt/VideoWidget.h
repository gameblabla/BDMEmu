#ifndef BDM_QT_VIDEOWIDGET_H
#define BDM_QT_VIDEOWIDGET_H

#include <QWidget>
#include <QImage>

extern "C" {
#include "bdm_video.h"
}

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    void setFrame(const bdm_video_t *video);
    void setIntegerScaling(bool enabled);
    bool integerScaling() const { return m_integerScaling; }

signals:
    void penDown(float x, float y);
    void penMove(float x, float y);
    void penUp(float x, float y);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;

private:
    QPointF toLcdF(const QPointF &p) const;
    QRect imageRect() const;
    QImage m_image;
    bool m_integerScaling;
};

#endif

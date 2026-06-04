#ifndef BDM_QT_MAINWINDOW_H
#define BDM_QT_MAINWINDOW_H

#include <QMainWindow>

#include "Engine.h"
#include "VideoWidget.h"

class QAction;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    Engine *engine() { return &m_engine; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void openCart();
    void openCartAndMedia();
    void saveState();
    void loadState();
    void toggleRun();
    void toggleScaling();
    void setAutoCalibration(bool enabled);

private:
    void createMenus();
    void applyKey(QKeyEvent *event, bool down);
    Engine m_engine;
    VideoWidget *m_video;
    QLabel *m_status;
    QAction *m_autoCalAction;
};

#endif

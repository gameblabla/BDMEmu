#ifndef BDM_QT_MAINWINDOW_H
#define BDM_QT_MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

#include "Engine.h"
#include "VideoWidget.h"

class QAction;
class QLabel;
class QPushButton;
class QWidget;

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
    void setHardwareButtonsVisible(bool visible);

private:
    void createMenus();
    void createHardwareButtons();
    void updateHardwareButtonsVisible();
    QPushButton *makePanelButton(const QString &text, int button);
    void applyKey(QKeyEvent *event, bool down);
    Engine m_engine;
    VideoWidget *m_video;
    QWidget *m_central;
    QWidget *m_topButtonBar;
    QPushButton *m_pageLeftButton;
    QPushButton *m_pageRightButton;
    QLabel *m_status;
    QAction *m_autoCalAction;
    QAction *m_visibleButtonsAction;
    QVector<QPushButton *> m_panelButtons;
};

#endif

#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>

extern "C" {
#include "bdm_input.h"
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_video(new VideoWidget(this)), m_status(new QLabel(this)), m_autoCalAction(nullptr) {
    setWindowTitle(QStringLiteral("Bandai Design Master Emulator"));
    setCentralWidget(m_video);
    createMenus();
    statusBar()->addWidget(m_status, 1);
    connect(&m_engine, &Engine::frameReady, m_video, &VideoWidget::setFrame);
    connect(&m_engine, &Engine::statusChanged, m_status, &QLabel::setText);
    connect(m_video, &VideoWidget::penDown, &m_engine, &Engine::penDown);
    connect(m_video, &VideoWidget::penMove, &m_engine, &Engine::penMove);
    connect(m_video, &VideoWidget::penUp, &m_engine, &Engine::penUp);
    resize(800, 600);
}

void MainWindow::createMenus() {
    QMenu *file = menuBar()->addMenu(QStringLiteral("File"));
    QAction *open = file->addAction(QStringLiteral("Open G Cart..."));
    connect(open, &QAction::triggered, this, &MainWindow::openCart);
    QAction *openPair = file->addAction(QStringLiteral("Open G Cart + M Media..."));
    connect(openPair, &QAction::triggered, this, &MainWindow::openCartAndMedia);
    file->addSeparator();
    QAction *save = file->addAction(QStringLiteral("Save State..."));
    connect(save, &QAction::triggered, this, &MainWindow::saveState);
    QAction *load = file->addAction(QStringLiteral("Load State..."));
    connect(load, &QAction::triggered, this, &MainWindow::loadState);
    file->addSeparator();
    QAction *quit = file->addAction(QStringLiteral("Quit"));
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu *emu = menuBar()->addMenu(QStringLiteral("Emulation"));
    QAction *run = emu->addAction(QStringLiteral("Pause / Resume"));
    run->setShortcut(Qt::Key_Space);
    connect(run, &QAction::triggered, this, &MainWindow::toggleRun);
    QAction *reset = emu->addAction(QStringLiteral("Reset"));
    reset->setShortcut(Qt::Key_R);
    connect(reset, &QAction::triggered, &m_engine, &Engine::reset);
    emu->addSeparator();
    m_autoCalAction = emu->addAction(QStringLiteral("Auto-calibrate on load/reset"));
    m_autoCalAction->setShortcut(Qt::Key_F10);
    m_autoCalAction->setCheckable(true);
    m_autoCalAction->setChecked(m_engine.autoCalibrationEnabled());
    connect(m_autoCalAction, &QAction::toggled, this, &MainWindow::setAutoCalibration);
    QAction *scale = emu->addAction(QStringLiteral("Toggle Integer Scaling"));
    scale->setShortcut(Qt::Key_F9);
    connect(scale, &QAction::triggered, this, &MainWindow::toggleScaling);
}

void MainWindow::openCart() {
    QString cart = QFileDialog::getOpenFileName(this, QStringLiteral("Open G cart"), QString(), QStringLiteral("ROM images (*.bin *.rom);;All files (*)"));
    if (!cart.isEmpty()) m_engine.load(cart);
}

void MainWindow::openCartAndMedia() {
    QString cart = QFileDialog::getOpenFileName(this, QStringLiteral("Open G cart"), QString(), QStringLiteral("ROM images (*.bin *.rom);;All files (*)"));
    if (cart.isEmpty()) return;
    QString media = QFileDialog::getOpenFileName(this, QStringLiteral("Open M media cart"), QString(), QStringLiteral("ROM images (*.bin *.rom);;All files (*)"));
    if (!media.isEmpty()) m_engine.load(cart, media);
}

void MainWindow::saveState() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save state"), QStringLiteral("bdm_state.bdmst"), QStringLiteral("BDM state (*.bdmst);;All files (*)"));
    if (!path.isEmpty()) m_engine.saveState(path);
}

void MainWindow::loadState() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load state"), QString(), QStringLiteral("BDM state (*.bdmst);;All files (*)"));
    if (!path.isEmpty()) m_engine.loadState(path);
}

void MainWindow::toggleRun() {
    if (m_engine.isRunning()) m_engine.stop();
    else m_engine.start();
}

void MainWindow::toggleScaling() { m_video->setIntegerScaling(!m_video->integerScaling()); }

void MainWindow::setAutoCalibration(bool enabled) { m_engine.setAutoCalibrationEnabled(enabled); }

void MainWindow::applyKey(QKeyEvent *event, bool down) {
    if (event->isAutoRepeat()) return;
    switch (event->key()) {
    case Qt::Key_Z: m_engine.setButton(BDM_BUTTON_A, down); break;
    case Qt::Key_X: m_engine.setButton(BDM_BUTTON_B, down); break;
    case Qt::Key_Return:
    case Qt::Key_Enter: m_engine.setButton(BDM_BUTTON_START, down); break;
    case Qt::Key_Backspace:
    case Qt::Key_Shift: m_engine.setButton(BDM_BUTTON_SELECT, down); break;
    case Qt::Key_Escape: if (down) close(); break;
    case Qt::Key_F5: if (down) m_engine.saveState(QString::fromUtf8(m_engine.options().state_slot_path)); break;
    case Qt::Key_F8: if (down) m_engine.loadState(QString::fromUtf8(m_engine.options().state_slot_path)); break;
    case Qt::Key_F11: if (down) isFullScreen() ? showNormal() : showFullScreen(); break;
    default: QMainWindow::keyPressEvent(event); break;
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) { applyKey(event, true); }
void MainWindow::keyReleaseEvent(QKeyEvent *event) { applyKey(event, false); }

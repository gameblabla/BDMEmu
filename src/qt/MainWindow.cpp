#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QStatusBar>

extern "C" {
#include "bdm_input.h"
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
    m_video(new VideoWidget(this)),
    m_central(new QWidget(this)),
    m_topButtonBar(nullptr),
    m_pageLeftButton(nullptr),
    m_pageRightButton(nullptr),
    m_status(new QLabel(this)),
    m_autoCalAction(nullptr),
    m_visibleButtonsAction(nullptr) {
    setWindowTitle(QStringLiteral("Bandai Design Master Emulator"));
    createHardwareButtons();
    setCentralWidget(m_central);
    createMenus();
    updateHardwareButtonsVisible();
    statusBar()->addWidget(m_status, 1);
    connect(&m_engine, &Engine::frameReady, m_video, &VideoWidget::setFrame);
    connect(&m_engine, &Engine::statusChanged, m_status, &QLabel::setText);
    connect(m_video, &VideoWidget::penDown, &m_engine, &Engine::penDown);
    connect(m_video, &VideoWidget::penMove, &m_engine, &Engine::penMove);
    connect(m_video, &VideoWidget::penUp, &m_engine, &Engine::penUp);
    resize(800, 600);
}

QPushButton *MainWindow::makePanelButton(const QString &text, int button) {
    QPushButton *b = new QPushButton(text, this);
    b->setFocusPolicy(Qt::NoFocus);
    b->setMinimumSize(42, 28);
    b->setAutoRepeat(false);
    connect(b, &QPushButton::pressed, this, [this, button]() { m_engine.setPanelButton(button, true); });
    connect(b, &QPushButton::released, this, [this, button]() { m_engine.setPanelButton(button, false); });
    m_panelButtons.push_back(b);
    return b;
}

void MainWindow::createHardwareButtons() {
    QVBoxLayout *outer = new QVBoxLayout(m_central);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);

    m_topButtonBar = new QWidget(m_central);
    QHBoxLayout *top = new QHBoxLayout(m_topButtonBar);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(4);
    top->addStretch(1);
    top->addWidget(makePanelButton(QStringLiteral("A"), BDM_BUTTON_MENU_A));
    top->addWidget(makePanelButton(QStringLiteral("B"), BDM_BUTTON_MENU_B));
    top->addWidget(makePanelButton(QStringLiteral("C"), BDM_BUTTON_MENU_C));
    top->addWidget(makePanelButton(QStringLiteral("D"), BDM_BUTTON_MENU_D));
    top->addWidget(makePanelButton(QStringLiteral("E"), BDM_BUTTON_MENU_E));
    top->addStretch(1);

    QWidget *middle = new QWidget(m_central);
    QHBoxLayout *mid = new QHBoxLayout(middle);
    mid->setContentsMargins(0, 0, 0, 0);
    mid->setSpacing(6);
    m_pageLeftButton = makePanelButton(QStringLiteral("◀"), BDM_BUTTON_PAGE_LEFT);
    m_pageRightButton = makePanelButton(QStringLiteral("▶"), BDM_BUTTON_PAGE_RIGHT);
    m_pageLeftButton->setToolTip(QStringLiteral("Page left"));
    m_pageRightButton->setToolTip(QStringLiteral("Page right"));
    mid->addWidget(m_pageLeftButton);
    mid->addWidget(m_video, 1);
    mid->addWidget(m_pageRightButton);

    outer->addWidget(m_topButtonBar);
    outer->addWidget(middle, 1);
}

void MainWindow::updateHardwareButtonsVisible() {
    bool visible = m_visibleButtonsAction && m_visibleButtonsAction->isChecked();
    if (m_topButtonBar) m_topButtonBar->setVisible(visible);
    if (m_pageLeftButton) m_pageLeftButton->setVisible(visible);
    if (m_pageRightButton) m_pageRightButton->setVisible(visible);
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

    QMenu *input = menuBar()->addMenu(QStringLiteral("Input"));
    m_visibleButtonsAction = input->addAction(QStringLiteral("Show hardware panel buttons"));
    m_visibleButtonsAction->setCheckable(true);
    m_visibleButtonsAction->setChecked(false);
    connect(m_visibleButtonsAction, &QAction::toggled, this, &MainWindow::setHardwareButtonsVisible);
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

void MainWindow::setHardwareButtonsVisible(bool visible) {
    (void)visible;
    updateHardwareButtonsVisible();
}

void MainWindow::applyKey(QKeyEvent *event, bool down) {
    if (event->isAutoRepeat()) return;
    switch (event->key()) {
    case Qt::Key_A:
    case Qt::Key_Z: m_engine.setPanelButton(BDM_BUTTON_MENU_A, down); break;
    case Qt::Key_B:
    case Qt::Key_X: m_engine.setPanelButton(BDM_BUTTON_MENU_B, down); break;
    case Qt::Key_C: m_engine.setPanelButton(BDM_BUTTON_MENU_C, down); break;
    case Qt::Key_D: m_engine.setPanelButton(BDM_BUTTON_MENU_D, down); break;
    case Qt::Key_E: m_engine.setPanelButton(BDM_BUTTON_MENU_E, down); break;
    case Qt::Key_Left:
    case Qt::Key_Backspace: m_engine.setPanelButton(BDM_BUTTON_PAGE_LEFT, down); break;
    case Qt::Key_Right:
    case Qt::Key_Return:
    case Qt::Key_Enter: m_engine.setPanelButton(BDM_BUTTON_PAGE_RIGHT, down); break;
    case Qt::Key_Escape: if (down) close(); break;
    case Qt::Key_F5: if (down) m_engine.saveState(QString::fromUtf8(m_engine.options().state_slot_path)); break;
    case Qt::Key_F8: if (down) m_engine.loadState(QString::fromUtf8(m_engine.options().state_slot_path)); break;
    case Qt::Key_F11: if (down) isFullScreen() ? showNormal() : showFullScreen(); break;
    default: QMainWindow::keyPressEvent(event); break;
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) { applyKey(event, true); }
void MainWindow::keyReleaseEvent(QKeyEvent *event) { applyKey(event, false); }

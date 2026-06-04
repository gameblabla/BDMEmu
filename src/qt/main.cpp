#include "MainWindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    if (argc > 1) {
        QTimer::singleShot(0, [&w, argc, argv]() { w.engine()->configureFromArgs(argc, argv); });
    }
    return app.exec();
}

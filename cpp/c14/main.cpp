#include "mainwindow.h"

#include <QApplication>
#include <QDebug>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    QApplication::setFont(QFont("Courier New", 14));
    MainWindow w;
    w.show();
    return QApplication::exec();
}

//#include <QtWidgets>
#include <QApplication>
#include <QtGui>
#include <QString>
#include "MainWindow.h"


int main (int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow mainwindow;
    mainwindow.show();

    return app.exec();
}



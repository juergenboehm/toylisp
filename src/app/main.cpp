#include "ui/mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("ToyLisp");
    QApplication::setOrganizationName("ToyLisp");

    application.setStyle(QStyleFactory::create("Fusion"));
    application.setFont(QFont("Segoe UI", 9)); // typische Windows-Systemschrift

    MainWindow window;
    window.show();
    return application.exec();
}

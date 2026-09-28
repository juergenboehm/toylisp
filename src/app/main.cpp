#include "ui/mainwindow.h"
#include <QApplication>
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    QApplication::setApplicationName("ToyLisp");
    QApplication::setOrganizationName("ToyLisp");
    MainWindow window;
    window.show();
    return application.exec();
}

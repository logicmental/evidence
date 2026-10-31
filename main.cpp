#include "main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("evidence");
    QApplication::setOrganizationName("evidence");
    QApplication::setApplicationDisplayName("evidence");

    MainWindow window;
    window.show();
    return app.exec();
}
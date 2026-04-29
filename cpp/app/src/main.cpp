#include "main_window.hpp"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("xlsOne"));
    QApplication::setOrganizationName(QStringLiteral("xlsOne"));

    MainWindow window;
    window.resize(1180, 760);
    window.show();
    return app.exec();
}


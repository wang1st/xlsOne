#include "gift_code_generator.hpp"

#include <QApplication>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("xlsone-gift-code-generator");
    app.setOrganizationName("xlsone");
    app.setApplicationVersion("1.0.4");

    QSettings::setDefaultFormat(QSettings::IniFormat);

    xlsone::GiftCodeGenerator window;
    window.show();

    return app.exec();
}

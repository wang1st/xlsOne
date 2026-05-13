#include "license_activation_dialog.hpp"
#include "main_window.hpp"
#include "dialog_utils.hpp"
#include "xlsone/core/license_manager.hpp"

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("xlsOne"));
    QApplication::setOrganizationName(QStringLiteral("xlsOne"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/xlsOne.png")));

    // Install translations
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::system(), QStringLiteral("qt"), QStringLiteral("_"), QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    QTranslator appTranslator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString& locale : uiLanguages) {
        const QString baseName = QStringLiteral("xlsone_") + QLocale(locale).name();
        if (appTranslator.load(baseName, QApplication::applicationDirPath() + QStringLiteral("/../i18n"))) {
            app.installTranslator(&appTranslator);
            break;
        }
    }

    // Domestic ARM64 Linux (UOS/Kylin/Phytium/Kunpeng) — always free, no license check
    if (xlsone::LicenseManager::isFreePlatform()) {
        MainWindow window;
        window.resize(1180, 760);
        window.show();
        return app.exec();
    }

    // License check for other platforms
    xlsone::LicenseManager licenseManager;

    const auto state = licenseManager.state();
    if (state == xlsone::LicenseState::Activated ||
        state == xlsone::LicenseState::Trial) {
        MainWindow window;
        window.resize(1180, 760);
        window.show();
        return app.exec();
    }

    // Show activation dialog
    LicenseActivationDialog dialog(&licenseManager);
    if (xlsone::ui::execDialogCentered(dialog) != QDialog::Accepted) {
        return 0; // User cancelled — quit
    }

    MainWindow window;
    window.resize(1180, 760);
    window.show();
    return app.exec();
}

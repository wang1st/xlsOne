#include "main_window.hpp"
#include "xlsone/core/license_manager.hpp"
#include "algorithm_keywords.hpp"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("xlsOne"));
    QApplication::setOrganizationName(QStringLiteral("xlsOne"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/xlsOne.png")));

    xlsone::AlgorithmKeywords::instance().load();

    // Install Qt base translations
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::system(), QStringLiteral("qt"), QStringLiteral("_"), QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    // Load saved language preference; fall back to system locale.
    QSettings settings;
    const QString savedLang = settings.value(QStringLiteral("AppLanguage")).toString();
    const QStringList uiLanguages = savedLang.isEmpty()
        ? QLocale::system().uiLanguages()
        : QStringList{savedLang};

    // Use a modern sans-serif (Heiti-like) font instead of the default SimSun.
    QFont appFont = QApplication::font();
    const QString effectiveLocale = uiLanguages.isEmpty() ? QStringLiteral("en") : uiLanguages.first();
    if (effectiveLocale.startsWith(QStringLiteral("zh"))) {
        appFont.setFamily(QStringLiteral("Microsoft YaHei"));
    } else if (effectiveLocale.startsWith(QStringLiteral("ja"))) {
        appFont.setFamily(QStringLiteral("Yu Gothic UI"));
    } else {
        appFont.setFamily(QStringLiteral("Segoe UI"));
    }
    QApplication::setFont(appFont);

    QTranslator appTranslator;
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

    MainWindow window;
    window.resize(1180, 760);
    window.show();
    return app.exec();
}

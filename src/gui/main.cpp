#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <cstring>
#include "MainWindow.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include "SwitchEngine.h"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QLocale::setDefault(QLocale(QLocale::Russian));
    QTranslator qtBase;
    if (qtBase.load(QLocale(QLocale::Russian), QStringLiteral("qtbase"), QStringLiteral("_"),
                    QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtBase);
    }
    app.setApplicationName("rocket-switcher-config");
    app.setApplicationDisplayName("Настройки Ракета");
    app.setOrganizationName("Raketa");
    app.setOrganizationDomain("rocket-switcher.local");

    // Use a system icon if available, fall back to a built-in placeholder
    app.setWindowIcon(QIcon::fromTheme("input-keyboard",
                      QIcon::fromTheme("preferences-desktop-keyboard")));

    bool trayOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tray") == 0) trayOnly = true;
    }
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    // The switcher lives in the menu-bar / notification area. Settings open from that icon.
    trayOnly = true;
#endif

    MainWindow w;
    if (!w.takeSingleInstance()) return 0;
    w.setupTray();
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    SwitchEngine::instance().start();
#endif
    if (!trayOnly) w.show();
    return app.exec();
}

/**************************************************************************
 *                                                                        *
 * SPDX-FileCopyrightText: 2015 Felix Rohrbach <kde@fxrh.de>              *
 *                                                                        *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *                                                                        *
 **************************************************************************/

#include <QtWidgets/QApplication>
#include <QtCore/QTranslator>
#include <QtCore/QLibraryInfo>
#include <QtCore/QCommandLineParser>
#include <QtCore/QLoggingCategory>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QStyleFactory>
#include <QtQuickControls2/QQuickStyle>

#include "mainwindow.h"
#include "logging_categories.h"
#include "linuxutils.h"

#include <Quotient/networksettings.h>

void loadTranslations(
        std::initializer_list<std::pair<QStringList, QString>> translationConfigs)
{
    for (const auto& [configNames, configPath]: translationConfigs)
        for (const auto& configName: configNames) {
            auto* translator = new QTranslator(qApp);
            bool loaded = false;
            // Check the current directory then configPath
            if (translator->load(QLocale(), configName, "_")
                || translator->load(QLocale(), configName, "_", configPath)) {
                auto path = translator->filePath();
                if ((loaded = QApplication::installTranslator(translator)))
                    qCDebug(MAIN).noquote()
                        << "Loaded translations from" << path;
                else
                    qCWarning(MAIN).noquote()
                        << "Failed to load translations from" << path;
            } else
                qCDebug(MAIN) << "No translations for" << configName << "at"
                              << configPath;
            if (!loaded)
                delete translator;
        }
}

int main( int argc, char* argv[] )
{
    using namespace Qt::StringLiterals;
    QApplication::setOrganizationName(u"Quotient"_s);
    QApplication::setApplicationName(u"quaternion"_s);
    QApplication::setApplicationDisplayName(u"Quaternion"_s);
    QApplication::setApplicationVersion(u"0.0.96.91"_s);
    QApplication::setDesktopFileName(u"io.github.quotient_im.Quaternion"_s);

    using Quotient::Settings;
    Settings::setLegacyNames(u"QMatrixClient"_s, u"quaternion"_s);
    Settings settings;

    QApplication app(argc, argv);
#if defined Q_OS_UNIX && !defined Q_OS_MAC
    // #681: When in Flatpak and unless overridden by configuration, set
    // the style to Breeze as it looks much fresher than Fusion that Qt
    // applications default to in Flatpak outside KDE. Although Qt docs
    // recommend to call setStyle() before constructing a QApplication object
    // (to make sure the style's palette is applied?) that doesn't work with
    // Breeze because it seems to make use of platform theme hints, which
    // in turn need a created QApplication object (see #700).
    const auto useBreezeStyle = settings.get("UI/use_breeze_style", inFlatpak());
    if (useBreezeStyle) {
        QApplication::setStyle("Breeze");
        QIcon::setThemeName("breeze");
        QIcon::setFallbackThemeName("breeze");
    } else
#endif
    {
        QQuickStyle::setFallbackStyle(u"Fusion"_s); // Looks better on desktops
//        QQuickStyle::setStyle("Material");
    }

    {
        auto font = QApplication::font();
        if (const auto fontFamily = settings.get<QString>("UI/Fonts/family");
            !fontFamily.isEmpty())
            font.setFamily(fontFamily);

        if (const auto fontPointSize =
                settings.value("UI/Fonts/pointSize").toReal();
            fontPointSize > 0)
            font.setPointSizeF(fontPointSize);

        qCInfo(MAIN) << "Using application font:" << font.toString();
        QApplication::setFont(font);
    }

    // We should not need to do the following, as quitOnLastWindowClosed is
    // set to "true" by default; might be a bug, see
    // https://forum.qt.io/topic/71112/application-does-not-quit
    QObject::connect(&app, &QApplication::lastWindowClosed, &app, [&app]{
        qCDebug(MAIN) << "Last window closed!";
        QApplication::postEvent(&app, new QEvent(QEvent::Quit));
    });

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main",
            "Quaternion - an IM client for the Matrix protocol"));
    parser.addHelpOption();
    parser.addVersionOption();

    QList<QCommandLineOption> options;
    QCommandLineOption locale { QStringLiteral("locale"),
        QApplication::translate("main", "Override locale"),
        QApplication::translate("main", "locale") };
    options.append(locale);
    QCommandLineOption hideMainWindow { QStringLiteral("hide-mainwindow"),
        QApplication::translate("main", "Hide main window on startup") };
    options.append(hideMainWindow);
    // Add more command line options before this line

    if (!parser.addOptions(options))
        Q_ASSERT_X(false, __FUNCTION__,
                   "Command line options are improperly defined, fix the code");
    parser.process(app);

    const auto overrideLocale = parser.value(locale);
    if (!overrideLocale.isEmpty())
    {
        QLocale::setDefault(QLocale(overrideLocale));
        qCInfo(MAIN) << "Using locale" << QLocale().name();
    }

// Extract a number from another macro and turn it to a const char[]
#define ITOA(i) #i

    loadTranslations({ { { "qt", "qtbase", "qtnetwork", "qtdeclarative", "qtmultimedia",
                           "qtquickcontrols", "qtquickcontrols2",
                           // QtKeychain tries to install its translations to Qt's path;
                           // try to look there, just in case (see also below)
                           "qtkeychain" },
                         QLibraryInfo::path(QLibraryInfo::TranslationsPath) },
                       { { "qtkeychain" },
                         QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                "qt" ITOA(QT_VERSION_MAJOR) "keychain/translations",
                                                QStandardPaths::LocateDirectory) },
                       { { "qt", "qtkeychain", "quotient", "quaternion" },
                         QStandardPaths::locate(QStandardPaths::AppLocalDataLocation, "translations",
                                                QStandardPaths::LocateDirectory) } });

#undef ITOA

    Quotient::NetworkSettings().setupApplicationProxy();

    MainWindow window;
    if (parser.isSet(hideMainWindow)) {
        qCDebug(MAIN) << "--- Hide time!";
        window.hide();
    }
    else {
        qCDebug(MAIN) << "--- Show time!";
        window.show();
    }

    return QApplication::exec();
}


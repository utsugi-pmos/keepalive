// SPDX-License-Identifier: GPL-2.0-or-later
//
// Entry point of the standalone application. Deliberately thin: everything it
// does is in KeepAliveBackend, the same object the Settings module uses.

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <KAboutData>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "backend.h"

int main(int argc, char *argv[])
{
    // QApplication and not QGuiApplication: Kirigami pulls in QtWidgets styles
    // for its dialogs, and with QGuiApplication they fall back to something
    // that does not match the rest of the phone.
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("keepalive"));

    KAboutData about(QStringLiteral("keepalive"),
                     i18n("Always-alive apps"),
                     QStringLiteral("1.0"),
                     i18n("Choose which applications do not close when you swipe their card"),
                     KAboutLicense::GPL_V2);
    KAboutData::setApplicationData(about);

    // Matches the .desktop file name, which is what lets the shell attach the
    // window to its launcher icon instead of showing a generic one.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.utsugi.keepalive"));

    KeepAliveBackend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("keepAliveBackend"), &backend);

    engine.loadFromModule("KeepAlive", "App");

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}

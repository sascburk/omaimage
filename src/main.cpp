#include "backend.h"
#include "customise.h"
#include "drives.h"
#include "systemtheme.h"
#include "writer.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>

#include <cstdio>

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    if (!args.isEmpty() && args.first() == QLatin1String("--self-test")) {
        QCoreApplication app(argc, argv);
        QString error;
        if (!customiseSelfTest(&error) || !drivesSelfTest(&error) || !writerSelfTest(&error)) {
            fprintf(stderr, "%s\n", error.toUtf8().constData());
            return 1;
        }
        fprintf(stdout, "self-test ok\n");
        return 0;
    }
    if (args.size() >= 2 && args.first() == QLatin1String("--write-job")) {
        QCoreApplication app(argc, argv);
        return runWriteJob(args.at(1), args.contains(QStringLiteral("--allow-file")));
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omaimage"));
    app.setOrganizationName(QStringLiteral("Omaimage"));
    app.setDesktopFileName(QStringLiteral("omaimage"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("omaimage")));
    QQuickStyle::setStyle(QStringLiteral("Material"));

    Backend backend;
    SystemTheme systemTheme;
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend, &Backend::setDarkMode);
    backend.setTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend, &Backend::setTextScale);

    const QString family = backend.fontFamily();
    QFont interfaceFont(family);
    const qreal base = interfaceFont.pointSizeF() > 0 ? interfaceFont.pointSizeF() : app.font().pointSizeF();
    interfaceFont.setPointSizeF(base * systemTheme.textScale());
    app.setFont(interfaceFont);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
                         for (const QQmlError &warning : warnings)
                             qWarning().noquote() << warning.toString();
                     });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}

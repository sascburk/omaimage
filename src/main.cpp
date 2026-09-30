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
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QTranslator>
#include <QUrl>

#include <cstdio>

void installTranslations(QCoreApplication *app) {
    const QString translationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    auto *qtTranslator = new QTranslator(app);
    if (qtTranslator->load(QLocale::system(), QStringLiteral("qtbase"), QStringLiteral("_"), translationsPath))
        app->installTranslator(qtTranslator);
    else
        delete qtTranslator;

    auto *appTranslator = new QTranslator(app);
    if (appTranslator->load(QLocale::system(), QStringLiteral("omaimage"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        app->installTranslator(appTranslator);
    else
        delete appTranslator;
}

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
        installTranslations(&app);
        return runWriteJob(args.at(1), args.contains(QStringLiteral("--allow-file")));
    }

    QApplication app(argc, argv);
    installTranslations(&app);
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

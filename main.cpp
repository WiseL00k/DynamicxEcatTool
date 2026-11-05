#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTranslator>

#include "App/ApplicationBootstrap.h"
#include "Backend/Commands/MitMotorCommand.h"
#include "Backend/Ethercat/EthercatBackend.h"

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle("Fusion");

    QGuiApplication app(argc, argv);
    QTranslator qtTranslator;
    const QString applicationTranslations = QCoreApplication::applicationDirPath()
        + QStringLiteral("/translations");
    const QString qtTranslations = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator.load(QStringLiteral("qt_zh_CN"), applicationTranslations)
        || qtTranslator.load(QStringLiteral("qt_zh_CN"), qtTranslations)
        || qtTranslator.load(QStringLiteral("qtbase_zh_CN"), qtTranslations)) {
        app.installTranslator(&qtTranslator);
    }
    const QString applicationFontFamily = App::applyApplicationFont(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/dynamicx_ecat_tool_logo.ico")));

    QQmlApplicationEngine engine;
    Backend::EthercatBackend ethercatBackend;
    Backend::MitMotorCommandQml mitMotorCommandQml;

    App::registerQmlContext(engine, applicationFontFamily, ethercatBackend, mitMotorCommandQml);

    const QUrl url(u"qrc:/DynamicxEcatToolQml/Main.qml"_qs);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);
    return app.exec();
}

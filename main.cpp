#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "App/ApplicationBootstrap.h"
#include "Backend/Commands/MitMotorCommand.h"
#include "Backend/Ethercat/EthercatBackend.h"

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle("Fusion");

    QGuiApplication app(argc, argv);
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

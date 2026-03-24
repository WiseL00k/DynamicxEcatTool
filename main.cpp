#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QDir>

#include "Backend/EthercatBackend.h"

int main(int argc, char *argv[])
{
    QDir dir(":/");
    QQuickStyle::setStyle("Fusion");
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/icons/dynamicx_ecat_tool_logo.ico"));
    QQmlApplicationEngine engine;

    // 创建后端对象
    Backend::EthercatBackend ethercatBackend;

    engine.addImportPath("qrc:/qml");
    // 注入到 QML
    engine.rootContext()->setContextProperty(
        "EthercatBackend",
        &ethercatBackend
        );

    const QUrl url(u"qrc:/DynamicxEcatToolQml/Main.qml"_qs);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection
        );

    engine.load(url);

    return app.exec();
}

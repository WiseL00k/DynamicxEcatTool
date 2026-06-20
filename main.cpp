#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include "Backend/EthercatBackend.h"
#include "Backend/MitMotorCommand.h"

namespace {
QString pickApplicationFontFamily()
{
    const QStringList installedFamilies = QFontDatabase::families();
    const QStringList candidates = {
#ifdef Q_OS_WIN
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("SimHei"),
#endif
#ifdef Q_OS_LINUX
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei"),
        QStringLiteral("WenQuanYi Zen Hei"),
#endif
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("Arial Unicode MS")
    };

    for (const QString& family : candidates) {
        if (installedFamilies.contains(family, Qt::CaseInsensitive))
            return family;
    }

    return QGuiApplication::font().family();
}

QString applyApplicationFont(QGuiApplication& app)
{
    const QString family = pickApplicationFontFamily();
    QFont font(family);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
    return family;
}
}

int main(int argc, char *argv[])
{
    QDir dir(":/");
    QQuickStyle::setStyle("Fusion");
    QGuiApplication app(argc, argv);
    const QString applicationFontFamily = applyApplicationFont(app);
    app.setWindowIcon(QIcon(":/icons/dynamicx_ecat_tool_logo.ico"));
    QQmlApplicationEngine engine;

    // 创建后端对象
    Backend::EthercatBackend ethercatBackend;
    Backend::MitMotorCommandQml mitMotorCommandQml;

    engine.addImportPath("qrc:/qml");
    // 注入到 QML
    engine.rootContext()->setContextProperty(
        "ApplicationFontFamily",
        applicationFontFamily
        );
    engine.rootContext()->setContextProperty(
        "EthercatBackend",
        &ethercatBackend
        );
    engine.rootContext()->setContextProperty(
        "MitMotorCommandQml",
        &mitMotorCommandQml
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

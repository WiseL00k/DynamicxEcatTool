#include "ApplicationBootstrap.h"

#include "Backend/Commands/MitMotorCommand.h"
#include "Backend/Ethercat/EthercatBackend.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>

namespace App {
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
        if (installedFamilies.contains(family, Qt::CaseInsensitive)) {
            return family;
        }
    }

    return QGuiApplication::font().family();
}

} // namespace

QString applyApplicationFont(QGuiApplication& app)
{
    const QString family = pickApplicationFontFamily();
    QFont font(family);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
    return family;
}

void registerQmlContext(
    QQmlApplicationEngine& engine,
    const QString& applicationFontFamily,
    Backend::EthercatBackend& ethercatBackend,
    Backend::MitMotorCommandQml& mitMotorCommandQml)
{
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("ApplicationFontFamily"), applicationFontFamily);
    engine.rootContext()->setContextProperty(QStringLiteral("EthercatBackend"), &ethercatBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("MitMotorCommandQml"), &mitMotorCommandQml);
}

} // namespace App

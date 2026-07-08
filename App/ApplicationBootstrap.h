#pragma once

#include <QString>

class QGuiApplication;
class QQmlApplicationEngine;

namespace Backend {
class EthercatBackend;
class MitMotorCommandQml;
}

namespace App {

QString applyApplicationFont(QGuiApplication& app);
void registerQmlContext(
    QQmlApplicationEngine& engine,
    const QString& applicationFontFamily,
    Backend::EthercatBackend& ethercatBackend,
    Backend::MitMotorCommandQml& mitMotorCommandQml);

} // namespace App

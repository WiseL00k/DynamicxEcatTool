#include "MitMotorCommand.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <QByteArray>

namespace Backend {

void MitMotorCommand::setPVTMAX(double pmax, double vmax, double tmax)
{
    if (!std::isfinite(pmax) || !std::isfinite(vmax) || !std::isfinite(tmax) ||
        pmax <= 0 || vmax <= 0 || tmax <= 0)
        throw std::invalid_argument("MIT 位置、速度和转矩范围必须为有限正数");
    pmax_ = pmax;
    vmax_ = vmax;
    tmax_ = tmax;
}

void MitMotorCommand::setCommand(double kp, double kd, double pos, double vel, double torque)
{
    if (pmax_ <= 0 || vmax_ <= 0 || tmax_ <= 0 || !std::isfinite(kp) || !std::isfinite(kd) ||
        !std::isfinite(pos) || !std::isfinite(vel) || !std::isfinite(torque) ||
        kp < 0 || kp > 500 || kd < 0 || kd > 5 ||
        std::abs(pos) > pmax_ || std::abs(vel) > vmax_ || std::abs(torque) > tmax_)
        throw std::invalid_argument("MIT 控制参数超出已配置的范围");
    const auto quantize = [](double value, double bound, unsigned maximum) {
        return static_cast<uint16_t>(std::clamp((value / bound + 1.0) * 0.5 * maximum,
                                               0.0, double(maximum)));
    };
    const auto p = quantize(pos, pmax_, 65535);
    const auto v = quantize(vel, vmax_, 4095);
    const auto kP = static_cast<uint16_t>(kp / 500.0 * 4095.0);
    const auto kD = static_cast<uint16_t>(kd / 5.0 * 4095.0);
    const auto t = quantize(torque, tmax_, 4095);

    uint8_t data[8]{};
    data[0] = static_cast<uint8_t>(p >> 8);
    data[1] = static_cast<uint8_t>(p);
    data[2] = static_cast<uint8_t>(v >> 4);
    data[3] = static_cast<uint8_t>(((v & 0xF) << 4) | (kP >> 8));
    data[4] = static_cast<uint8_t>(kP);
    data[5] = static_cast<uint8_t>(kD >> 4);
    data[6] = static_cast<uint8_t>(((kD & 0xF) << 4) | (t >> 8));
    data[7] = static_cast<uint8_t>(t);

    std::memcpy(&rawFrame_, data, sizeof(data));
}

MitMotorCommandQml::MitMotorCommandQml(QObject* parent)
    : QObject(parent)
{}

QVariantMap previewMitFrame(const QVariantMap& data)
{
    const auto invalid = [](const QString& error) {
        return QVariantMap{{"valid", false}, {"error", error}, {"bytes", QVariantList{}}, {"hex", QString{}}};
    };
    double values[8]{};
    const char* keys[]{"pos", "vel", "kp", "kd", "torque", "PMAX", "VMAX", "TMAX"};
    for (int i = 0; i < 8; ++i) {
        bool ok = false;
        const auto value = data.value(QLatin1String(keys[i]));
        values[i] = value.toDouble(&ok);
        if (!data.contains(QLatin1String(keys[i])) || value.metaType().id() == QMetaType::Bool || !ok || !std::isfinite(values[i]))
            return invalid(QStringLiteral("%1 必须是有限数值").arg(QLatin1String(keys[i])));
    }
    const double pos = values[0], vel = values[1], kp = values[2], kd = values[3], torque = values[4];
    const double pmax = values[5], vmax = values[6], tmax = values[7];
    if (pmax <= 0 || vmax <= 0 || tmax <= 0) return invalid(QStringLiteral("PMAX、VMAX、TMAX 必须大于零"));
    if (kp < 0 || kp > 500 || kd < 0 || kd > 5) return invalid(QStringLiteral("Kp 范围为 0–500，Kd 范围为 0–5"));
    if (std::abs(pos) > pmax || std::abs(vel) > vmax || std::abs(torque) > tmax)
        return invalid(QStringLiteral("位置、速度或力矩超出所填电机量程"));
    MitMotorCommand command;
    command.setPVTMAX(pmax, vmax, tmax);
    command.setCommand(kp, kd, pos, vel, torque);

    const quint64 raw = command.getRawCommand();
    const auto* bytes = reinterpret_cast<const uint8_t*>(&raw);

    QVariantList list;
    for (int i = 0; i < 8; ++i) {
        list << bytes[i];
    }

    const auto hex = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes), 8).toHex(' ').toUpper());
    return {{"valid", true}, {"error", QString{}}, {"bytes", list}, {"hex", hex}};
}

QVariantList MitMotorCommandQml::buildMitFrame(const QVariantMap& data)
{
    return previewMitFrame(data).value(QStringLiteral("bytes")).toList();
}

} // namespace Backend

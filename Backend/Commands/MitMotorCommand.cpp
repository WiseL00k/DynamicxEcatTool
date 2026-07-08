#include "MitMotorCommand.h"

#include <cstring>

namespace Backend {

void MitMotorCommand::setPVTMAX(double pmax, double vmax, double tmax)
{
    pmax_ = pmax;
    vmax_ = vmax;
    tmax_ = tmax;
}

void MitMotorCommand::setCommand(double kp, double kd, double pos, double vel, double torque)
{
    const auto p = static_cast<uint16_t>((pos + pmax_) * (65535.0 / (pmax_ * 2.0)));
    const auto v = static_cast<uint16_t>((vel + vmax_) * (4095.0 / (vmax_ * 2.0)));
    const auto kP = static_cast<uint16_t>(kp * (4095.0 / 500.0));
    const auto kD = static_cast<uint16_t>(kd * (4095.0 / 5.0));
    const auto t = static_cast<uint16_t>((torque + tmax_) * (4095.0 / (tmax_ * 2.0)));

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

QVariantList MitMotorCommandQml::buildMitFrame(const QVariantMap& data)
{
    const double pos = data.value(QStringLiteral("pos"), 0.0).toDouble();
    const double vel = data.value(QStringLiteral("vel"), 0.0).toDouble();
    const double kp = data.value(QStringLiteral("kp"), 0.0).toDouble();
    const double kd = data.value(QStringLiteral("kd"), 0.0).toDouble();
    const double torque = data.value(QStringLiteral("torque"), 0.0).toDouble();
    const double pmax = data.value(QStringLiteral("PMAX")).toDouble();
    const double vmax = data.value(QStringLiteral("VMAX")).toDouble();
    const double tmax = data.value(QStringLiteral("TMAX")).toDouble();

    MitMotorCommand command;
    command.setPVTMAX(pmax, vmax, tmax);
    command.setCommand(kp, kd, pos, vel, torque);

    const quint64 raw = command.getRawCommand();
    const auto* bytes = reinterpret_cast<const uint8_t*>(&raw);

    QVariantList list;
    for (int i = 0; i < 8; ++i) {
        list << bytes[i];
    }

    return list;
}

} // namespace Backend

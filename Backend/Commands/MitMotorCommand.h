#ifndef MITMOTORCOMMAND_H
#define MITMOTORCOMMAND_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>

namespace Backend {

class MitMotorCommand
{
public:
    void setPVTMAX(double pmax, double vmax, double tmax);
    void setCommand(double kp, double kd, double pos, double vel, double torque);

    uint64_t getRawCommand() const { return rawFrame_; }

private:
    double pmax_{0.0};
    double vmax_{0.0};
    double tmax_{0.0};
    uint64_t rawFrame_{0};
};

class MitMotorCommandQml : public QObject
{
    Q_OBJECT

public:
    explicit MitMotorCommandQml(QObject* parent = nullptr);

public slots:
    QVariantList buildMitFrame(const QVariantMap& data);
};

} // namespace Backend

#endif // MITMOTORCOMMAND_H

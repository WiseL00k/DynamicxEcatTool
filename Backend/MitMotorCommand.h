#ifndef MITMOTORCOMMAND_H
#define MITMOTORCOMMAND_H

#include <QObject>
#include <QVariantMap>

namespace Backend {

class MitMotorCommand
{
public:
    void setPVTMAX(double &PMAX, double& VMAX, double& TMAX) {
        PMAX_=PMAX; VMAX_=VMAX; TMAX_=TMAX;
    }
    void setCommand(double kp, double kd, double pos, double vel, double torque){
        uint16_t p = static_cast<uint16_t>((pos + PMAX_) * (65535.0f / (PMAX_ * 2) ));
        uint16_t v = static_cast<uint16_t>((vel + VMAX_) * (4095.0f / (VMAX_ * 2)));
        uint16_t k_p = static_cast<uint16_t>(kp * (4095.0f / 500.0f));
        uint16_t k_d = static_cast<uint16_t>(kd * (4095.0f / 5.0f));
        uint16_t t = static_cast<uint16_t>((torque + TMAX_) * (4095.0f / (TMAX_ * 2)));

        uint8_t data[8] = {0};

        data[0] = (p >> 8);
        data[1] = p;
        data[2] = (v >> 4);
        data[3] = ((v & 0xF) << 4) | (k_p >> 8);
        data[4] = k_p;
        data[5] = (k_d >> 4);
        data[6] = ((k_d & 0xF) << 4) | (t >> 8);
        data[7] = t;

        memcpy(&raw_frame_, data, 8);
    }

    inline uint64_t getRawCommand() const {return raw_frame_;}

private:
    double pos_;
    double vel_;
    double kp_;
    double kd_;
    double torque_;

    double PMAX_,VMAX_,TMAX_;
    uint64_t raw_frame_{};
};

class MitMotorCommandQml : public QObject
{
    Q_OBJECT
public:
    explicit MitMotorCommandQml(QObject *parent = nullptr);

public slots:
    QVariantList buildMitFrame(const QVariantMap &data)
    {
        double pos = data.value("pos", 0.0).toDouble();
        double vel = data.value("vel", 0.0).toDouble();
        double kp  = data.value("kp", 0.0).toDouble();
        double kd  = data.value("kd", 0.0).toDouble();
        double tor = data.value("torque", 0.0).toDouble();
        double PMAX =data.value("PMAX").toDouble();
        double VMAX =data.value("VMAX").toDouble();
        double TMAX =data.value("TMAX").toDouble();

        MitMotorCommand command;
        command.setPVTMAX(PMAX, VMAX, TMAX);
        command.setCommand(kp,kd,pos,vel,tor);

        quint64 raw = command.getRawCommand();

        QVariantList list;
        uint8_t *p = reinterpret_cast<uint8_t*>(&raw);

        for (int i = 0; i < 8; i++) {
            list << p[i];
        }

        return list;
    }

signals:
};

}
#endif // MITMOTORCOMMAND_H

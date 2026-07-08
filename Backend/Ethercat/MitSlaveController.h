#pragma once

#include <QString>
#include <QVariantList>

namespace soem_interface {
class EcatMasterBus;
}

namespace Backend {

class MitSlaveController
{
public:
    bool enableMotors(soem_interface::EcatMasterBus* master, QString& errorMessage) const;
    bool disableMotors(soem_interface::EcatMasterBus* master, QString& errorMessage) const;
    bool sendFrame(soem_interface::EcatMasterBus* master, int canBus, int canId, const QVariantList& data, QString& errorMessage) const;
    bool clearFrame(soem_interface::EcatMasterBus* master, int canBus, int canId, QString& errorMessage) const;
};

} // namespace Backend

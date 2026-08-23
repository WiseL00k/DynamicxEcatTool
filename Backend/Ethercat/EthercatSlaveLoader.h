#pragma once

#include "Backend/Models/DeviceStatusModel.h"
#include <QString>
#include <memory>
#include <string>

namespace soem_interface {
class EcatMasterBus;
}

namespace Backend {

struct SlaveLoadResult
{
    bool ok{false};
    int slaveCount{0};
    QString errorMessage;
};

class EthercatSlaveLoader
{
public:
    static SlaveLoadResult loadFromFile(
        const std::string& configFilePath,
        soem_interface::EcatMasterBus& master,
        DeviceStatusModel& deviceModel);

    static SlaveLoadResult addMitDebugSlave(soem_interface::EcatMasterBus& master);
};

} // namespace Backend


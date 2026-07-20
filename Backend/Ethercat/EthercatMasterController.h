#pragma once

#include "SOEM_interface/SoemUtils.h"

#include <QString>
#include <memory>
#include <string>

class DeviceStatusModel;

namespace soem_interface {
class EcatMasterBus;
struct BusScanResult;
struct BusStateResult;
}

namespace Backend {

struct MasterStartResult
{
    bool ok{false};
    int slaveCount{0};
    soem_interface::error::SoemInterfaceErrorCode errorCode{soem_interface::error::NoError};
    QString logMessage;
    QString detailMessage;
};

class EthercatMasterController
{
public:
    ~EthercatMasterController();

    bool hasMaster() const;
    bool isConnected() const;
    int slaveCount() const;
    soem_interface::EcatMasterBus* master() const;
    std::shared_ptr<soem_interface::EcatMasterBus> masterRef() const;

    MasterStartResult startTest(const std::string& nicName);
    MasterStartResult startCommunication(
        const std::string& nicName,
        const std::string& configFilePath,
        DeviceStatusModel& deviceModel);
    MasterStartResult enterPreOp(const std::string& nicName);
    MasterStartResult enterMitDebugMode(const std::string& nicName);
    soem_interface::BusScanResult startExplorer(const std::string& nicName);

    void stop();
    void closePreOp();
    void stopExplorer();
    void reset();

private:
    bool validateSlaveCount(int configuredSlaveCount, QString& logMessage);

    std::shared_ptr<soem_interface::EcatMasterBus> master_;
    bool connected_{false};
    int configuredSlaveCount_{0};
};

} // namespace Backend

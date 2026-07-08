#include "MitSlaveController.h"

#include "Backend/Config/DxSlaveConfigurationParser.h"
#include "Backend/Ethercat/Slaves/MitEcatSlave.h"
#include "SOEM_interface/EcatMasterBus.h"

#include <cstring>

namespace Backend {
namespace {

bool isValidCanBus(int canBus)
{
    return canBus == static_cast<int>(DxSlave::CanBus::CAN0)
        || canBus == static_cast<int>(DxSlave::CanBus::CAN1);
}

rm_ecat_slave::mit::MitEcatSlave* mitSlave(soem_interface::EcatMasterBus* master)
{
    if (master == nullptr) {
        return nullptr;
    }

    for (const auto& slave : master->registeredSlaves()) {
        auto* mit = dynamic_cast<rm_ecat_slave::mit::MitEcatSlave*>(slave.get());
        if (mit != nullptr) {
            return mit;
        }
    }

    return nullptr;
}

bool validateCanTarget(int canBus, int canId, QString& errorMessage)
{
    if (!isValidCanBus(canBus)) {
        errorMessage = QStringLiteral("CAN Bus参数错误");
        return false;
    }

    if (canId < 1 || canId > static_cast<int>(rm_ecat_slave::mit::motorNumEachBus)) {
        errorMessage = QStringLiteral("CAN ID参数错误");
        return false;
    }

    return true;
}

} // namespace

bool MitSlaveController::enableMotors(soem_interface::EcatMasterBus* master, QString& errorMessage) const
{
    auto* slave = mitSlave(master);
    if (slave == nullptr) {
        errorMessage = QStringLiteral("MIT从站未连接");
        return false;
    }

    slave->enableMotors();
    return true;
}

bool MitSlaveController::disableMotors(soem_interface::EcatMasterBus* master, QString& errorMessage) const
{
    auto* slave = mitSlave(master);
    if (slave == nullptr) {
        errorMessage = QStringLiteral("MIT从站未连接");
        return false;
    }

    slave->disableMotors();
    return true;
}

bool MitSlaveController::sendFrame(
    soem_interface::EcatMasterBus* master,
    int canBus,
    int canId,
    const QVariantList& data,
    QString& errorMessage) const
{
    if (!validateCanTarget(canBus, canId, errorMessage)) {
        return false;
    }

    if (data.size() != 8) {
        errorMessage = QStringLiteral("MIT帧长度错误");
        return false;
    }

    auto* slave = mitSlave(master);
    if (slave == nullptr) {
        errorMessage = QStringLiteral("MIT从站未连接");
        return false;
    }

    uint8_t frame[8]{};
    for (int i = 0; i < 8; ++i) {
        bool ok = false;
        const uint value = data[i].toUInt(&ok);
        if (!ok || value > 255U) {
            errorMessage = QStringLiteral("MIT帧字节参数错误");
            return false;
        }
        frame[i] = static_cast<uint8_t>(value);
    }

    uint64_t mitFrame{};
    std::memcpy(&mitFrame, frame, sizeof(frame));
    if (!slave->setCanCommand(static_cast<DxSlave::CanBus>(canBus), static_cast<size_t>(canId), mitFrame)) {
        errorMessage = QStringLiteral("MIT帧目标参数错误");
        return false;
    }

    return true;
}

bool MitSlaveController::clearFrame(
    soem_interface::EcatMasterBus* master,
    int canBus,
    int canId,
    QString& errorMessage) const
{
    if (!validateCanTarget(canBus, canId, errorMessage)) {
        return false;
    }

    auto* slave = mitSlave(master);
    if (slave == nullptr) {
        errorMessage = QStringLiteral("MIT从站未连接");
        return false;
    }

    if (!slave->clearCanCommand(static_cast<DxSlave::CanBus>(canBus), static_cast<size_t>(canId))) {
        errorMessage = QStringLiteral("MIT帧目标参数错误");
        return false;
    }

    return true;
}

} // namespace Backend

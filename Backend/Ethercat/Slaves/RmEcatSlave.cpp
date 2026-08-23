#include "RmEcatSlave.h"

#include "SOEM_interface/EcatMasterBus.h"

namespace rm_ecat_slave{
namespace standard {

bool Statusword::isOnline(CanBus bus, size_t id) const
{
    if (!isValidMotorId(id)) {
        return false;
    }
    const size_t i = getIndex(bus, id);
    return (statusword_ & (1U << i)) > 0;
}

bool Statusword::isImuOnline(CanBus bus) const
{
    static bool imus_online_flag[2]{};
    static uint32_t imus_timeout[2]{};

    if (!isAngularVelocityUpdated(bus) || !isLinearAccelerationUpdated(bus)) {
        imus_timeout[static_cast<int>(bus)]++;
    } else {
        imus_online_flag[static_cast<int>(bus)] = true;
        imus_timeout[static_cast<int>(bus)] = 0;
    }
    if (imus_timeout[static_cast<int>(bus)] > 20) {
        imus_timeout[static_cast<int>(bus)] = 20;
        imus_online_flag[static_cast<int>(bus)] = false;
    }
    return imus_online_flag[static_cast<int>(bus)];
}

bool Statusword::isAngularVelocityUpdated(CanBus bus) const
{
    return (statusword_ & (1U << (4 * static_cast<uint8_t>(bus) + 16))) > 0;
}

bool Statusword::isLinearAccelerationUpdated(CanBus bus) const
{
    return (statusword_ & (1U << (4 * static_cast<uint8_t>(bus) + 16 + 1))) > 0;
}

bool Statusword::isTriggered(CanBus bus) const
{
    return (statusword_ & (1U << (4 * static_cast<uint8_t>(bus) + 16 + 2))) > 0;
}

bool Statusword::isTriggerEnabled(CanBus bus) const
{
    return (statusword_ & (1U << (4 * static_cast<uint8_t>(bus) + 16 + 3))) > 0;
}

bool RmEcatSlave::startup()
{
    pdoInfo_.rxPdoSize_ = sizeof(RxPdo);
    pdoInfo_.txPdoSize_ = sizeof(TxPdo);
    return true;
}

void RmEcatSlave::updateRead()
{
    std::lock_guard<std::mutex> lock(readingMutex_);
    bus_->readTxPdo(static_cast<uint16_t>(address_), sizeof(txPdo), &txPdo);
    reading_.setStatusword(txPdo.statusword_);
}

void RmEcatSlave::updateWrite()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    bus_->writeRxPdo(static_cast<uint16_t>(address_), sizeof(rxPdo), &rxPdo);
}

void RmEcatSlave::shutdown()
{}

uint32_t RmEcatSlave::statuswordRaw() const
{
    Reading reading;
    getReading(reading);
    return reading.getStatusword().getRaw();
}

std::vector<soem_interface::EcatSlaveBase::DeviceOnlineStatus> RmEcatSlave::collectDeviceOnlineStatuses() const
{
    Reading reading;
    getReading(reading);

    std::vector<DeviceOnlineStatus> statuses;
    statuses.reserve(configuration_.can0MotorConfigurations_.size() + configuration_.can1MotorConfigurations_.size() + 2);

    for (const auto& [id, motorConfig] : configuration_.can0MotorConfigurations_) {
        statuses.push_back({motorConfig.name_, reading.getStatusword().isOnline(CanBus::CAN0, id)});
    }

    for (const auto& [id, motorConfig] : configuration_.can1MotorConfigurations_) {
        statuses.push_back({motorConfig.name_, reading.getStatusword().isOnline(CanBus::CAN1, id)});
    }

    if (configuration_.can0ImuConfiguration_ != nullptr) {
        statuses.push_back({configuration_.can0ImuConfiguration_->name_, reading.getStatusword().isImuOnline(CanBus::CAN0)});
    }

    if (configuration_.can1ImuConfiguration_ != nullptr) {
        statuses.push_back({configuration_.can1ImuConfiguration_->name_, reading.getStatusword().isImuOnline(CanBus::CAN1)});
    }

    return statuses;
}

} // namespace standard
} // namespace rm_ecat_slave

#include "MitEcatSlave.h"

#include "SOEM_interface/EcatMasterBus.h"

namespace rm_ecat_slave{
namespace mit {

bool Statusword::isOnline(CanBus bus, size_t id) const
{
    if (!isValidMotorId(id)) {
        return false;
    }
    const size_t i = getIndex(bus, id);
    return (statusword_ & (1U << i)) > 0;
}

bool MitEcatSlave::startup()
{
    pdoInfo_.rxPdoSize_ = sizeof(RxPdo);
    pdoInfo_.txPdoSize_ = sizeof(TxPdo);
    return true;
}

void MitEcatSlave::updateRead()
{
    std::lock_guard<std::mutex> lock(readingMutex_);
    bus_->readTxPdo(static_cast<uint16_t>(address_), sizeof(txPdo), &txPdo);
    reading_.setStatusword(txPdo.statusword_);
}

void MitEcatSlave::updateWrite()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    command_.toRxPdo(rxPdo);
    bus_->writeRxPdo(static_cast<uint16_t>(address_), sizeof(rxPdo), &rxPdo);
}

void MitEcatSlave::shutdown()
{}

uint32_t MitEcatSlave::statuswordRaw() const
{
    Reading reading;
    getReading(reading);
    return reading.getStatusword().getRaw();
}

std::vector<soem_interface::EcatSlaveBase::DeviceOnlineStatus> MitEcatSlave::collectDeviceOnlineStatuses() const
{
    Reading reading;
    getReading(reading);

    std::vector<DeviceOnlineStatus> statuses;
    statuses.reserve(configuration_.can0MotorConfigurations_.size() + configuration_.can1MotorConfigurations_.size());

    for (const auto& [id, motorConfig] : configuration_.can0MotorConfigurations_) {
        statuses.push_back({motorConfig.name_, reading.getStatusword().isOnline(CanBus::CAN0, id)});
    }

    for (const auto& [id, motorConfig] : configuration_.can1MotorConfigurations_) {
        statuses.push_back({motorConfig.name_, reading.getStatusword().isOnline(CanBus::CAN1, id)});
    }

    return statuses;
}

void MitEcatSlave::clearCommand()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    command_.clear();
}

void MitEcatSlave::enableMotors()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    command_.enableMotors();
}

void MitEcatSlave::disableMotors()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    command_.disableMotors();
}

bool MitEcatSlave::setCanCommand(CanBus bus, size_t id, uint64_t command)
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    return command_.setCanCommand(bus, id, command);
}

bool MitEcatSlave::clearCanCommand(CanBus bus, size_t id)
{
    return setCanCommand(bus, id, MitClearFrame);
}

} // namespace mit
} // namespace rm_ecat_slave

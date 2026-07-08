#pragma once

#include "Backend/Config/DxSlaveConfigurationParser.h"
#include "SOEM_interface/EcatSlaveBase.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>

namespace rm_ecat_slave{
using namespace Backend::DxSlave;

namespace standard {

static constexpr size_t motorNumEachBus = 8;

inline bool isValidMotorId(size_t id) {
    return id >= 1 && id <= motorNumEachBus;
}

inline size_t getIndex(CanBus bus, size_t id) {
    return static_cast<uint16_t>(bus) * motorNumEachBus + id - 1;
}

#pragma pack(push,1)
struct RxPdo {
    uint32_t controlword_;
    int16_t can0MotorCommnads_[8];
    int16_t can1MotorCommnads_[8];
    uint8_t digital_outputs_;
};
#pragma pack(pop)

#pragma pack(push,1)
struct TxPdo {
    uint16_t can0MotorPositions_[8];
    uint16_t can1MotorPositions_[8];
    int16_t can0MotorVelocities_[8];
    int16_t can1MotorVelocities_[8];
    int16_t can0MotorCurrents_[8];
    int16_t can1MotorCurrents_[8];
    uint8_t can0MotorTemperatures_[8];
    uint8_t can1MotorTemperatures_[8];
    int16_t can0ImuLinearAcceleration_[3];
    int16_t can1ImuLinearAcceleration_[3];
    int16_t can0ImuAngularVelocity_[3];
    int16_t can1ImuAngularVelocity_[3];
    uint8_t digital_inputs_;
    int16_t dbus_data_1_[8];
    int16_t dbus_data_2_[8];
    uint32_t statusword_;
};
#pragma pack(pop)

class Statusword {
public:
    friend std::ostream& operator<<(std::ostream& os, const Statusword& statusword);

    uint32_t getRaw() const { return statusword_; }
    void setRaw(uint32_t raw) { statusword_ = raw; }

    bool isOnline(CanBus bus, size_t id) const;
    bool isImuOnline(CanBus bus) const;
    bool isAngularVelocityUpdated(CanBus bus) const;
    bool isLinearAccelerationUpdated(CanBus bus) const;
    bool isTriggered(CanBus bus) const;
    bool isTriggerEnabled(CanBus bus) const;

private:
    uint32_t statusword_{0};
};

class Reading
{
public:
    void setStatusword(uint32_t statusword) { statusword_.setRaw(statusword); }
    Statusword getStatusword() const { return statusword_; }
private:
    Statusword statusword_;
};

class Command
{
public:
    void setDigitalOutput(uint8_t id, bool value) {
        digitalOutputs_ &= ~(static_cast<uint8_t>(1) << id);
        digitalOutputs_ |= (static_cast<uint8_t>(value) << id);
    }
private:
    uint8_t digitalOutputs_{0};
};

class RmEcatSlave : public soem_interface::EcatSlaveBase
{
public:
    RmEcatSlave() = default;
    RmEcatSlave(const std::string& name, soem_interface::EcatMasterBus* bus, uint32_t address)
        : EcatSlaveBase(bus, address), name_(name), type_("Rm") {}
    RmEcatSlave(const std::string& name, uint32_t address)
        : EcatSlaveBase(nullptr, address), name_(name), type_("Rm") {}

    std::string getName() const override { return name_; }
    std::string getType() const override { return type_; }
    PdoInfo getCurrentPdoInfo() const override { return pdoInfo_; }

    bool startup() override;
    void updateRead() override;
    void updateWrite() override;
    void shutdown() override;

    uint32_t statuswordRaw() const override;
    std::vector<DeviceOnlineStatus> collectDeviceOnlineStatuses() const override;

    void getReading(Reading& reading) const {
        std::lock_guard<std::mutex> lock(readingMutex_);
        reading = reading_;
    }

    DxSlaveConfiguration& getConfiguration() { return configuration_; }
    const DxSlaveConfiguration& getConfiguration() const { return configuration_; }

    void setCommand(const Command& command) {
        std::lock_guard<std::mutex> lock(commandMutex_);
        command_ = command;
    }

    void setConfiguration(const DxSlaveConfiguration& configuration) {
        configuration_ = configuration;
    }

private:
    mutable std::mutex readingMutex_, commandMutex_;
    Reading reading_{};
    Command command_{};
    RxPdo rxPdo{};
    TxPdo txPdo{};
    std::string name_, type_;
    DxSlaveConfiguration configuration_;
    PdoInfo pdoInfo_;
};

} // namespace standard
} // namespace rm_ecat_slave

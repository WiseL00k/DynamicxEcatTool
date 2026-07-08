#pragma once

#include "Backend/Config/DxSlaveConfigurationParser.h"
#include "SOEM_interface/EcatSlaveBase.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>

namespace rm_ecat_slave{
using namespace Backend::DxSlave;

namespace mit {

static constexpr size_t motorNumEachBus = 9;
static constexpr uint64_t MitClearFrame = 0xFF070000F07FFF7FULL;

inline bool isValidMotorId(size_t id) {
    return id >= 1 && id <= motorNumEachBus;
}

inline size_t getIndex(CanBus bus, size_t id) {
    return static_cast<uint16_t>(bus) * motorNumEachBus + id - 1;
}

#pragma pack(push,1)
struct RxPdo {
    uint32_t controlword_;
    uint64_t can0Commands_[motorNumEachBus];
    uint64_t can1Commands_[motorNumEachBus];
    uint8_t digitalOutputs_;
};
#pragma pack(pop)

#pragma pack(push,1)
struct TxPdo {
    uint64_t can0Measurement_[motorNumEachBus];
    uint64_t can1Measurement_[motorNumEachBus];
    uint8_t digitalInputs_;
    uint32_t statusword_;
};
#pragma pack(pop)

enum StateTransition {
    DisableToEnable = 1,
    EnableToDisable = 2,
} typedef StateTransition;

class Statusword {
public:
    friend std::ostream& operator<<(std::ostream& os, const Statusword& statusword);

    uint32_t getRaw() const { return statusword_; }
    void setRaw(uint32_t raw) { statusword_ = raw; }

    bool isOnline(CanBus bus, size_t id) const;

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
    Command() { clear(); }

    void setDigitalOutput(uint8_t id, bool value)
    {
        digitalOutputs_ &= ~(static_cast<uint8_t>(1) << id);
        digitalOutputs_ |= (static_cast<uint8_t>(value) << id);
    }

    void clear()
    {
        controlword_ = 0;
        digitalOutputs_ = 0;

        for (auto& bus : canCommands_) {
            for (auto& cmd : bus) {
                cmd = MitClearFrame;
            }
        }
    }

    bool setCanCommand(CanBus bus, size_t id, uint64_t command)
    {
        const auto busIndex = static_cast<size_t>(bus);
        if (busIndex >= 2 || !isValidMotorId(id)) {
            return false;
        }

        canCommands_[busIndex][id - 1] = command;
        return true;
    }

    void toRxPdo(RxPdo& pdo) const
    {
        pdo.controlword_ = controlword_;
        std::memcpy(pdo.can0Commands_, canCommands_[0], sizeof(pdo.can0Commands_));
        std::memcpy(pdo.can1Commands_, canCommands_[1], sizeof(pdo.can1Commands_));
        pdo.digitalOutputs_ = digitalOutputs_;
    }

    void enableMotors() { controlword_ = DisableToEnable; }
    void disableMotors() { controlword_ = EnableToDisable; }

private:
    uint32_t controlword_{0};
    uint64_t canCommands_[2][motorNumEachBus]{};
    uint8_t digitalOutputs_{0};
};

class MitEcatSlave : public soem_interface::EcatSlaveBase
{
public:
    MitEcatSlave() = default;
    MitEcatSlave(const std::string& name, soem_interface::EcatMasterBus* bus, uint32_t address)
        : EcatSlaveBase(bus, address), name_(name), type_("Mit") {}
    MitEcatSlave(const std::string& name, uint32_t address)
        : EcatSlaveBase(nullptr, address), name_(name), type_("Mit") {}

    std::string getName() const override { return name_; }
    std::string getType() const override { return type_; }
    PdoInfo getCurrentPdoInfo() const override { return pdoInfo_; }

    bool startup() override;
    void updateRead() override;
    void updateWrite() override;
    void shutdown() override;

    uint32_t statuswordRaw() const override;
    std::vector<DeviceOnlineStatus> collectDeviceOnlineStatuses() const override;

    void clearCommand();
    void enableMotors();
    void disableMotors();
    bool setCanCommand(CanBus bus, size_t id, uint64_t command);
    bool clearCanCommand(CanBus bus, size_t id);

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

using MitEcatSlavePtr = std::shared_ptr<MitEcatSlave>;

} // namespace mit
} // namespace rm_ecat_slave


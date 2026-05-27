#pragma once

#include "SOEM_interface/EcatSlaveBase.h"
#include "Backend/DxSlaveConfigurationParser.h"

namespace rm_ecat_slave{
using namespace Backend::DxSlave;

namespace standard {

static constexpr size_t motorNumEachBus = 8;

inline size_t getIndex(CanBus bus, size_t id) {
    return static_cast<uint16_t>(bus) * motorNumEachBus + id - 1;
}

#pragma pack(push,1)
struct RxPdo {
    uint32_t controlword_;
    int16_t can0MotorCommnads_[8];
    int16_t can1MotorCommnads_[8];
    uint8_t digital_outputs_;
} ;
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
} ;
#pragma pack(pop)

class Statusword {
public:
    friend std::ostream& operator<<(std::ostream& os, const Statusword& statusword);

    uint32_t getRaw() const { return statusword_; }
    void setRaw(uint32_t raw) { statusword_ = raw; }

    // Motor
    bool isOnline(CanBus bus, size_t id) const {
        size_t i = getIndex(bus, id);
        return (statusword_ & (1 << i)) > 0; };

    // Imu
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
    void setStatusword(uint32_t statusword) { statusword_.setRaw(statusword); };
    Statusword getStatusword() const { return statusword_; }
private:
    Statusword statusword_;

};

class Command
{
public:
    void setDigitalOutput(uint8_t id, bool value) {
        digitalOutputs_ &= ~(static_cast<uint8_t>(1) << id);
        digitalOutputs_ |= (static_cast<uint8_t>(value) << id);}
private:
  uint8_t digitalOutputs_{0};
};

class RmEcatSlave : public soem_interface::EcatSlaveBase
{
public:
    // Constructor
    RmEcatSlave() = default;
    RmEcatSlave(const std::string& name, soem_interface::EcatMasterBus* bus, uint32_t address) : EcatSlaveBase(bus, address), name_(name) ,type_("Rm"){}
    RmEcatSlave(const std::string& name, uint32_t address) : EcatSlaveBase(nullptr, address), name_(name) , type_("Rm"){}

    std::string getName() const override { return name_; }
    std::string getType() const override { return type_; }
    PdoInfo getCurrentPdoInfo() const override { return pdoInfo_; }

    /**
   * @brief      Startup non-ethercat specific objects for the slave
   *
   * @return     True if succesful
   */
    bool startup() override;

    /**
   * @brief      Called during reading the ethercat bus. Use this method to
   *             extract readings from the ethercat bus buffer
   */
    void updateRead() override;

    /**
   * @brief      Called during writing to the ethercat bus. Use this method to
   *             stage a command for the slave
   */
    void updateWrite() override;

    /**
   * @brief      Used to shutdown slave specific objects
   */
    void shutdown() override;

    void getReading(Reading& reading) {
        std::lock_guard<std::mutex> lock(readingMutex_);
        reading = reading_;
    }

    DxSlaveConfiguration& getConfiguration()
    {
        return configuration_;
    }

     void setCommand(const Command& command) {
        std::lock_guard<std::mutex> lock(commandMutex_);
        command_ = command;
    }

    void setConfiguration(const DxSlaveConfiguration& configuration){
     configuration_ = configuration;
    }
private:
    std::mutex readingMutex_, commandMutex_;
    Reading reading_{};
    Command command_{};
    RxPdo rxPdo{};
    TxPdo txPdo{};
    std::string name_,type_;
    DxSlaveConfiguration configuration_;
    PdoInfo pdoInfo_;
};

}

namespace mit
{

static constexpr size_t motorNumEachBus = 8;

inline size_t getIndex(CanBus bus, size_t id) {
    return static_cast<uint16_t>(bus) * motorNumEachBus + id - 1;
}

#pragma pack(push,1)
struct RxPdo {
    uint32_t controlword_;
    uint64_t can0Commands_[8];
    uint64_t can1Commands_[8];
    uint8_t digitalOutputs_;
} ;
#pragma pack(pop)

#pragma pack(push,1)
struct TxPdo {
    uint64_t can0Measurement_[8];
    uint64_t can1Measurement_[8];
    uint8_t digitalInputs_;
    uint32_t statusword_;
} ;
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

    // Motor
    bool isOnline(CanBus bus, size_t id) const{
        size_t i = getIndex(bus, id);
        return (statusword_ & (1 << i)) > 0; };

private:
    uint32_t statusword_{0};
};

class Reading
{
public:
    void setStatusword(uint32_t statusword) { statusword_.setRaw(statusword); };
    Statusword getStatusword() const { return statusword_; }
private:
    Statusword statusword_;

};

class Command
{
public:
    Command()
    {
        clear();
    }

    void setDigitalOutput(uint8_t id, bool value)
    {
        digitalOutputs_ &= ~(static_cast<uint8_t>(1) << id);
        digitalOutputs_ |= (static_cast<uint8_t>(value) << id);
    }

    void clear()
    {
        controlword_ = 0;

        for(auto& bus : canCommands_)
        {
            for(auto& cmd : bus)
            {
                // auto tor = 0;
                // auto vel = 0;
                // auto pos = 0;
                // auto kp = 0;
                // auto kd = 0;
                // uint8_t data[8] = {0};
                // data[0] = (pos >> 8);
                // data[1] = pos;
                // data[2] = (vel >> 4);
                // data[3] = ((vel & 0xF) << 4) | (kp >> 8);
                // data[4] = kp;
                // data[5] = (kd >> 4);
                // data[6] = ((kd & 0xF) << 4) | (tor >> 8);
                // data[7] = tor;
                // memcpy(&cmd, data, 8);

                cmd = 0xFF070000F07FFF7FULL;
            }
        }
    }

    void setCanCommand(CanBus bus, size_t id, uint64_t command)
    {
        auto busIndex = static_cast<size_t>(bus);

        if(busIndex >= 2 || id > 8)
            return;

        canCommands_[busIndex][id - 1] = command;
    }

    void toRxPdo(RxPdo& pdo) const
    {
        pdo.controlword_ = controlword_;

        memcpy(pdo.can0Commands_, canCommands_[0], sizeof(pdo.can0Commands_));
        memcpy(pdo.can1Commands_, canCommands_[1], sizeof(pdo.can1Commands_));

        pdo.digitalOutputs_ = digitalOutputs_;
    }

    void enableMotors(){ controlword_ = 1;}
    void disableMotors() {controlword_ = 2;}

private:
    uint32_t controlword_{0};
    uint64_t canCommands_[2][8]{};
    uint8_t digitalOutputs_{0};
};

class MitEcatSlave : public soem_interface::EcatSlaveBase
{
public:
    // Constructor
    MitEcatSlave() = default;
    MitEcatSlave(const std::string& name, soem_interface::EcatMasterBus* bus, uint32_t address) : EcatSlaveBase(bus, address), name_(name) ,type_("Mit"){}
    MitEcatSlave(const std::string& name, uint32_t address) : EcatSlaveBase(nullptr, address), name_(name) , type_("Mit"){}

    std::string getName() const override { return name_; }
    std::string getType() const override { return type_; }
    PdoInfo getCurrentPdoInfo() const override { return pdoInfo_; }

    /**
   * @brief      Startup non-ethercat specific objects for the slave
   *
   * @return     True if succesful
   */
    bool startup() override;

    /**
   * @brief      Called during reading the ethercat bus. Use this method to
   *             extract readings from the ethercat bus buffer
   */
    void updateRead() override;

    /**
   * @brief      Called during writing to the ethercat bus. Use this method to
   *             stage a command for the slave
   */
    void updateWrite() override;

    /**
   * @brief      Used to shutdown slave specific objects
   */
    void shutdown() override;

    void clearCommand();

    Command& getCommandHandle() { return command_;}

    void getReading(Reading& reading) {
        std::lock_guard<std::mutex> lock(readingMutex_);
        reading = reading_;
    }

    DxSlaveConfiguration& getConfiguration()
    {
        return configuration_;
    }

    void setCommand(const Command& command) {
        std::lock_guard<std::mutex> lock(commandMutex_);
        command_ = command;
    }

    void setConfiguration(const DxSlaveConfiguration& configuration){
        configuration_ = configuration;
    }
private:
    std::mutex readingMutex_, commandMutex_;
    Reading reading_{};
    Command command_{};
    RxPdo rxPdo{};
    TxPdo txPdo{};
    std::string name_,type_;
    DxSlaveConfiguration configuration_;
    PdoInfo pdoInfo_;
};

using MitEcatSlavePtr = std::shared_ptr<MitEcatSlave>;

}
}

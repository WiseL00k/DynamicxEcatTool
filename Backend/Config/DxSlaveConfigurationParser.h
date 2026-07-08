#pragma once

#include "Backend/Config/ConfigurationParserBase.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Backend {
namespace DxSlave {

enum class CanBus { CAN0 = 0, CAN1 = 1 };

class MotorConfiguration {
public:
    std::string name_;
    std::string type_;
    uint16_t maxOut_{0};
    double torqueFactorIntegerToNm_{0.0};
    double torqueFactorNmToInteger_{0.0};
};

class ImuConfiguration {
public:
    std::string name_;
    double angularVelFactorIntegerToRadPerSecond_ = 0.0010652644;
    double linearAccelFactorIntegerToMeterPerSecondSquared_ = 0.0017944335;
    double angularVelBias_[3]{0.0, 0.0, 0.0};
    double gainAccel_ = 0.0003;
    double biasAlpha_ = 0.01;
    bool doBiasEstimation_ = false;
    bool doAdaptiveGain_ = true;
};

class GpioConfiguration {
public:
    std::string name_;
    uint16_t mode_{0};
};

class DxSlaveConfiguration {
public:
    std::unordered_map<uint8_t, MotorConfiguration> can0MotorConfigurations_;
    std::unordered_map<uint8_t, MotorConfiguration> can1MotorConfigurations_;
    std::unordered_map<uint8_t, GpioConfiguration> gpioConfigurations_;
    std::shared_ptr<ImuConfiguration> can0ImuConfiguration_{nullptr};
    std::shared_ptr<ImuConfiguration> can1ImuConfiguration_{nullptr};
    int motorCount_{0};
};

class DxSlaveConfigurationParser : public YamlConfigurationParserBase<DxSlaveConfiguration>
{
public:
    explicit DxSlaveConfigurationParser(const std::string& filename);
    explicit DxSlaveConfigurationParser(const YAML::Node& node);
    ~DxSlaveConfigurationParser() override = default;

protected:
    void parseConfiguration(const YAML::Node& configNode) override;
};

} // namespace DxSlave
} // namespace Backend


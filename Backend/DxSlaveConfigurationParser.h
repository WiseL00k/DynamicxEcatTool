#pragma once

#include "Backend/ConfigurationParserBase.h"
#include <unordered_map>

namespace Backend {
namespace DxSlave {

enum class CanBus { CAN0 = 0, CAN1 = 1 };

class MotorConfiguration {
public:
    std::string name_;
    std::string type_;
    uint16_t maxOut_;
    double torqueFactorIntegerToNm_;
    double torqueFactorNmToInteger_;
};

class ImuConfiguration {
public:
    std::string name_;
    double angularVelFactorIntegerToRadPerSecond_ = 0.0010652644;
    double linearAccelFactorIntegerToMeterPerSecondSquared_ = 0.0017944335;
    double angularVelBias_[3]{0};
    double gainAccel_ = 0.0003;
    double biasAlpha_ = 0.01;
    bool doBiasEstimation_ = false;
    bool doAdaptiveGain_ = true;
};

class GpioConfiguration {
public:
    std::string name_;
    uint16_t mode_;
};

class DxSlaveConfiguration{
public:
    std::unordered_map<uint8_t, MotorConfiguration> can0MotorConfigurations_;
    std::unordered_map<uint8_t, MotorConfiguration> can1MotorConfigurations_;
    std::unordered_map<uint8_t, GpioConfiguration> gpioConfigurations_;
    std::shared_ptr<ImuConfiguration> can0ImuConfiguration_{nullptr}, can1ImuConfiguration_{nullptr};
    int motorCount_;
};

class DxSlaveConfigurationParser : public YamlConfigurationParserBase<DxSlaveConfiguration>
{
public:
    explicit DxSlaveConfigurationParser(const std::string& filename)
        : YamlConfigurationParserBase(filename)
    {
        parse();   // 子类构造完成后再解析
    }

    explicit DxSlaveConfigurationParser(const YAML::Node& node)
        : YamlConfigurationParserBase(node)
    {
        parse();
    }

    ~DxSlaveConfigurationParser() override = default;
protected:
    void parseConfiguration(const YAML::Node& configNode) override;
};
}

} // namespace Backend

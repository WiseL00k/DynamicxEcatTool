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
    void parseConfiguration(const YAML::Node& configNode) override
    {
        // =========================
        // 1. 解析 CAN 电机
        // =========================
        if (configNode["can_motors"].IsDefined()) {
            YAML::Node motors = configNode["can_motors"];
            if (!motors.IsSequence()) {
                return;
            }

            int motorCount = 0;
            for (YAML::const_iterator it = motors.begin(); it != motors.end(); ++it) {
                YAML::Node child = *it;

                uint16_t id = 0;
                uint16_t bus = 0;

                getValueFromFile(child, "can_id", id);
                getValueFromFile(child, "can_bus", bus);

                std::string type = child["type"].as<std::string>();

                MotorConfiguration motorConfig;
                motorConfig.name_ = child["name"].as<std::string>();

                if (bus == static_cast<uint16_t>(CanBus::CAN0)) {
                    configuration_.can0MotorConfigurations_.insert(
                        std::make_pair(static_cast<uint8_t>(id), motorConfig));
                }
                else if (bus == static_cast<uint16_t>(CanBus::CAN1)) {
                    configuration_.can1MotorConfigurations_.insert(
                        std::make_pair(static_cast<uint8_t>(id), motorConfig));
                }
                else {
                    printf("[DxDebugConfigurationParser] Unknown CAN bus");
                }
                ++motorCount;
            }
            configuration_.motorCount_ = motorCount;
        }

        // =========================
        // 2. 解析 GPIO
        // =========================
        if (configNode["gpios"].IsDefined()) {
            YAML::Node gpios = configNode["gpios"];
            if (!gpios.IsSequence()) {
                return;
            }

            for (YAML::const_iterator it = gpios.begin(); it != gpios.end(); ++it) {
                YAML::Node child = *it;

                GpioConfiguration gpioConfig;
                getValueFromFile(child, "name", gpioConfig.name_);
                getValueFromFile(child, "mode", gpioConfig.mode_);

                uint16_t pin = 0;
                getValueFromFile(child, "pin", pin);

                configuration_.gpioConfigurations_.insert(
                    std::make_pair(static_cast<uint8_t>(pin), gpioConfig));
            }
        }
    }
};
}

} // namespace Backend

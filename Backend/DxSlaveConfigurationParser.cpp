#include "DxSlaveConfigurationParser.h"

namespace Backend {
namespace DxSlave {


void DxSlaveConfigurationParser::parseConfiguration(const YAML::Node& configNode)
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

    // =========================
    // 3. 解析 IMU
    // =========================
    if (configNode["can_imus"].IsDefined()) {
        YAML::Node imus = configNode["can_imus"];
        if (!imus.IsSequence()) {
            return;
        }
        for (YAML::const_iterator it = imus.begin(); it != imus.end(); ++it) {
            YAML::Node child = *it;
            uint16_t bus = 0;
            getValueFromFile(child, "can_bus", bus);
            ImuConfiguration imuConfiguration;
            getValueFromFile(child, "name", imuConfiguration.name_);
            getValueFromFile(child, "gain_accel", imuConfiguration.gainAccel_);
            getValueFromFile(child, "bias_alpha_", imuConfiguration.biasAlpha_);
            getValueFromFile(child, "do_bias_estimation_", imuConfiguration.doBiasEstimation_);
            getValueFromFile(child, "do_adaptive_gain", imuConfiguration.doAdaptiveGain_);
            if (child["bias"].IsDefined()) {
                YAML::Node bias = child["bias"];
                if (bias.IsSequence() && bias.size() == 3) {
                    for (size_t i = 0; i < 3; ++i) {
                        imuConfiguration.angularVelBias_[i] = bias[i].as<double>();
                    }
                } else {
                    std::cout << "[rm_ecat::ConfigurationParser::parseConfiguration] Bias must be a sequence of 3 elements" << std::endl;
                }
            }
            if (bus == 0) {
                configuration_.can0ImuConfiguration_ = std::make_shared<ImuConfiguration>(imuConfiguration);
            } else if (bus == 1) {
                configuration_.can1ImuConfiguration_ = std::make_shared<ImuConfiguration>(imuConfiguration);
            } else {
                std::cout << "[rm_ecat::ConfigurationParser::parseConfiguration] Unknown can bus" << std::endl;
            }
        }
    }
}

}
} // namespace Backend

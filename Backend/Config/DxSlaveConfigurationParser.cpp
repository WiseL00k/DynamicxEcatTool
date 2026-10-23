#include "DxSlaveConfigurationParser.h"

#include <stdexcept>

namespace Backend {
namespace DxSlave {
namespace {

bool isCanBus(uint16_t bus)
{
    return bus == static_cast<uint16_t>(CanBus::CAN0)
        || bus == static_cast<uint16_t>(CanBus::CAN1);
}

void requireParsed(bool parsed, const std::string& field, const std::string& diagnostic)
{
    if (!parsed) {
        throw std::runtime_error("[DxSlaveConfigurationParser] 字段缺失或无效：" + field
            + (diagnostic.empty() ? "" : "；" + diagnostic));
    }
}

void validateCanId(uint16_t id)
{
    if (id == 0 || id > 9) {
        throw std::runtime_error("[DxSlaveConfigurationParser] CAN ID 超出 1–9 范围：" + std::to_string(id));
    }
}

} // namespace

DxSlaveConfigurationParser::DxSlaveConfigurationParser(const std::string& filename)
    : YamlConfigurationParserBase(filename)
{
    parse();
}

DxSlaveConfigurationParser::DxSlaveConfigurationParser(const YAML::Node& node)
    : YamlConfigurationParserBase(node)
{
    parse();
}

void DxSlaveConfigurationParser::parseConfiguration(const YAML::Node& configNode)
{
    if (configNode["can_motors"].IsDefined()) {
        const YAML::Node motors = configNode["can_motors"];
        if (!motors.IsSequence()) {
            throw std::runtime_error("[DxSlaveConfigurationParser] can_motors 必须为列表");
        }

        int motorCount = 0;
        for (const YAML::Node& child : motors) {
            uint16_t id = 0;
            uint16_t bus = 0;

            requireParsed(getValueFromFile(child, "can_id", id), "can_id", lastValueDiagnostic_);
            requireParsed(getValueFromFile(child, "can_bus", bus), "can_bus", lastValueDiagnostic_);
            validateCanId(id);

            if (!isCanBus(bus)) {
                throw std::runtime_error("[DxSlaveConfigurationParser] 无效的 CAN 总线（仅支持 0 或 1）：" + std::to_string(bus));
            }

            MotorConfiguration motorConfig;
            requireParsed(getValueFromFile(child, "name", motorConfig.name_), "name", lastValueDiagnostic_);
            requireParsed(getValueFromFile(child, "type", motorConfig.type_), "type", lastValueDiagnostic_);
            getValueFromFile(child, "max_out", motorConfig.maxOut_);
            getValueFromFile(child, "torque_factor_integer_to_nm", motorConfig.torqueFactorIntegerToNm_);
            getValueFromFile(child, "torque_factor_nm_to_integer", motorConfig.torqueFactorNmToInteger_);

            auto& target = (bus == static_cast<uint16_t>(CanBus::CAN0))
                ? configuration_.can0MotorConfigurations_
                : configuration_.can1MotorConfigurations_;
            const auto [_, inserted] = target.insert(std::make_pair(static_cast<uint8_t>(id), motorConfig));
            if (!inserted) {
                throw std::runtime_error("[DxSlaveConfigurationParser] CAN 总线上的 ID 重复，总线/ID："
                    + std::to_string(bus) + ": " + std::to_string(id));
            }
            ++motorCount;
        }

        configuration_.motorCount_ = motorCount;
    }

    if (configNode["gpios"].IsDefined()) {
        const YAML::Node gpios = configNode["gpios"];
        if (!gpios.IsSequence()) {
            throw std::runtime_error("[DxSlaveConfigurationParser] gpios 必须为列表");
        }

        for (const YAML::Node& child : gpios) {
            GpioConfiguration gpioConfig;
            requireParsed(getValueFromFile(child, "name", gpioConfig.name_), "name", lastValueDiagnostic_);
            requireParsed(getValueFromFile(child, "mode", gpioConfig.mode_), "mode", lastValueDiagnostic_);

            uint16_t pin = 0;
            requireParsed(getValueFromFile(child, "pin", pin), "pin", lastValueDiagnostic_);
            if (pin > 255) {
                throw std::runtime_error("[DxSlaveConfigurationParser] GPIO 引脚编号超出 0–255 范围：" + std::to_string(pin));
            }

            const auto [_, inserted] = configuration_.gpioConfigurations_.insert(
                std::make_pair(static_cast<uint8_t>(pin), gpioConfig));
            if (!inserted) {
                throw std::runtime_error("[DxSlaveConfigurationParser] GPIO 引脚编号重复：" + std::to_string(pin));
            }
        }
    }

    if (configNode["can_imus"].IsDefined()) {
        const YAML::Node imus = configNode["can_imus"];
        if (!imus.IsSequence()) {
            throw std::runtime_error("[DxSlaveConfigurationParser] can_imus 必须为列表");
        }

        for (const YAML::Node& child : imus) {
            uint16_t bus = 0;
            requireParsed(getValueFromFile(child, "can_bus", bus), "can_bus", lastValueDiagnostic_);
            if (!isCanBus(bus)) {
                throw std::runtime_error("[DxSlaveConfigurationParser] 无效的 CAN 总线（仅支持 0 或 1）：" + std::to_string(bus));
            }

            ImuConfiguration imuConfiguration;
            requireParsed(getValueFromFile(child, "name", imuConfiguration.name_), "name", lastValueDiagnostic_);
            getValueFromFile(child, "gain_accel", imuConfiguration.gainAccel_);
            getValueFromFile(child, "bias_alpha_", imuConfiguration.biasAlpha_);
            getValueFromFile(child, "do_bias_estimation_", imuConfiguration.doBiasEstimation_);
            getValueFromFile(child, "do_adaptive_gain", imuConfiguration.doAdaptiveGain_);

            if (child["bias"].IsDefined()) {
                const YAML::Node bias = child["bias"];
                if (!bias.IsSequence() || bias.size() != 3) {
                    throw std::runtime_error("[DxSlaveConfigurationParser] bias 必须为包含 3 个元素的列表");
                }
                for (size_t i = 0; i < 3; ++i) {
                    imuConfiguration.angularVelBias_[i] = bias[i].as<double>();
                }
            }

            if (bus == static_cast<uint16_t>(CanBus::CAN0)) {
                if (configuration_.can0ImuConfiguration_ != nullptr) {
                    throw std::runtime_error("[DxSlaveConfigurationParser] CAN0 的 IMU 配置重复");
                }
                configuration_.can0ImuConfiguration_ = std::make_shared<ImuConfiguration>(imuConfiguration);
            } else {
                if (configuration_.can1ImuConfiguration_ != nullptr) {
                    throw std::runtime_error("[DxSlaveConfigurationParser] CAN1 的 IMU 配置重复");
                }
                configuration_.can1ImuConfiguration_ = std::make_shared<ImuConfiguration>(imuConfiguration);
            }
        }
    }
}

} // namespace DxSlave
} // namespace Backend

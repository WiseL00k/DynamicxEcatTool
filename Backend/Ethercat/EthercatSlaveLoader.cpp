#include "EthercatSlaveLoader.h"

#include "Backend/Config/DxSlaveConfigurationParser.h"
#include "Backend/Config/EcatSlaveConfigurationParser.h"
#include "Backend/Ethercat/Slaves/EcatSlaveFactory.h"
#include "Backend/Ethercat/Slaves/MitEcatSlave.h"
#include "SOEM_interface/EcatMasterBus.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

namespace Backend {
namespace {

using SlaveEntry = EcatSlaveConfiguration;

std::vector<SlaveEntry> orderedSlaveEntries(const EcatDeviceConfiguration& configuration)
{
    std::vector<SlaveEntry> entries;
    entries.reserve(configuration.slaveConfigurations_.size());

    for (const auto& [address, slave] : configuration.slaveConfigurations_) {
        Q_UNUSED(address);
        entries.push_back(slave);
    }

    std::sort(entries.begin(), entries.end(), [](const SlaveEntry& lhs, const SlaveEntry& rhs) {
        return lhs.address < rhs.address;
    });

    return entries;
}

void addDeviceStatusItems(
    int address,
    const SlaveEntry& slave,
    const DxSlave::DxSlaveConfiguration& configuration,
    DeviceStatusModel& deviceModel)
{
    deviceModel.addSlaveHeader(
        QStringLiteral("从站%1: %2 (%3 电机)")
            .arg(address)
            .arg(QString::fromStdString(slave.name))
            .arg(configuration.motorCount_));

    for (const auto& [id, motorConfig] : configuration.can0MotorConfigurations_) {
        deviceModel.addMotor(QString::fromStdString(motorConfig.name_), 0, id);
    }

    for (const auto& [id, motorConfig] : configuration.can1MotorConfigurations_) {
        deviceModel.addMotor(QString::fromStdString(motorConfig.name_), 1, id);
    }

    if (configuration.can0ImuConfiguration_ != nullptr) {
        deviceModel.addImu(
            QString::fromStdString(configuration.can0ImuConfiguration_->name_),
            static_cast<int>(DxSlave::CanBus::CAN0));
    }

    if (configuration.can1ImuConfiguration_ != nullptr) {
        deviceModel.addImu(
            QString::fromStdString(configuration.can1ImuConfiguration_->name_),
            static_cast<int>(DxSlave::CanBus::CAN1));
    }
}

} // namespace

SlaveLoadResult EthercatSlaveLoader::loadFromFile(
    const std::string& configFilePath,
    soem_interface::EcatMasterBus& master,
    DeviceStatusModel& deviceModel)
{
    try {
        EcatSlaveConfigurationParser parser(configFilePath);
        const EcatDeviceConfiguration ecatConfiguration = parser.getConfiguration();
        const std::vector<SlaveEntry> slaves = orderedSlaveEntries(ecatConfiguration);

        deviceModel.clear();

        for (const SlaveEntry& slave : slaves) {
            DxSlave::DxSlaveConfigurationParser dxParser(slave.configuration_file_path);
            const DxSlave::DxSlaveConfiguration dxConfiguration = dxParser.getConfiguration();

            QString errorMessage;
            auto runtimeSlave = createEcatSlave(slave, dxConfiguration, master, errorMessage);
            if (!runtimeSlave) {
                return {false, static_cast<int>(slaves.size()), errorMessage};
            }

            std::cout << "Slave " << slave.address << " : " << slave.name << " , Type: " << slave.type << std::endl;
            std::cout << "  CAN0 Motors: " << dxConfiguration.can0MotorConfigurations_.size() << std::endl;
            std::cout << "  CAN1 Motors: " << dxConfiguration.can1MotorConfigurations_.size() << std::endl;

            if (!master.addSlave(runtimeSlave)) {
                return {
                    false,
                    static_cast<int>(slaves.size()),
                    QStringLiteral("重复的从站配置: %1").arg(QString::fromStdString(slave.name))
                };
            }

            addDeviceStatusItems(slave.address, slave, dxConfiguration, deviceModel);
        }

        return {true, static_cast<int>(slaves.size()), {}};
    } catch (const std::exception& ex) {
        return {false, 0, QString::fromLocal8Bit(ex.what())};
    } catch (...) {
        return {false, 0, QStringLiteral("未知配置解析错误")};
    }
}

SlaveLoadResult EthercatSlaveLoader::addMitDebugSlave(soem_interface::EcatMasterBus& master)
{
    auto slave = std::make_shared<rm_ecat_slave::mit::MitEcatSlave>("mit", &master, 1);

    if (!master.addSlave(slave)) {
        return {false, 1, QStringLiteral("MIT调试从站添加失败")};
    }

    return {true, 1, {}};
}

} // namespace Backend

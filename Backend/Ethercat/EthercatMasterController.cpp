#include "EthercatMasterController.h"

#include "Backend/Ethercat/EthercatSlaveLoader.h"
#include "SOEM_interface/EcatMasterBus.h"

namespace Backend {

EthercatMasterController::~EthercatMasterController()
{
    stop();
}

bool EthercatMasterController::hasMaster() const
{
    return master_ != nullptr;
}

bool EthercatMasterController::isConnected() const
{
    return connected_;
}

int EthercatMasterController::slaveCount() const
{
    return configuredSlaveCount_;
}

soem_interface::EcatMasterBus* EthercatMasterController::master() const
{
    return master_.get();
}

std::shared_ptr<soem_interface::EcatMasterBus> EthercatMasterController::masterRef() const
{
    return master_;
}

void EthercatMasterController::reset()
{
    master_.reset();
    connected_ = false;
    configuredSlaveCount_ = 0;
}

void EthercatMasterController::stop()
{
    if (master_) {
        master_->stop();
    }

    reset();
}

void EthercatMasterController::closePreOp()
{
    if (master_) {
        master_->closeMaster();
    }

    reset();
}

bool EthercatMasterController::validateSlaveCount(int configuredSlaveCount, QString& logMessage)
{
    if (!master_) {
        return false;
    }

    if (configuredSlaveCount == master_->slaveCount()) {
        return true;
    }

    logMessage = QStringLiteral("初始化失败，从站数量不一致！\n实际从站数量:%1 配置从站数量:%2")
        .arg(master_->slaveCount())
        .arg(configuredSlaveCount);
    master_->stop();
    connected_ = false;
    return false;
}

MasterStartResult EthercatMasterController::startTest(const std::string& nicName)
{
    if (master_) {
        return {false, configuredSlaveCount_};
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName);

    const auto errorCode = master_->startTest();
    connected_ = (errorCode == soem_interface::error::NoError);

    if (!connected_) {
        const QString logMessage = QStringLiteral("初始化失败，无法连接到网卡 %1").arg(QString::fromStdString(nicName));
        reset();
        return {false, 0, errorCode, logMessage};
    }

    configuredSlaveCount_ = master_->slaveCount();
    return {true, configuredSlaveCount_};
}

MasterStartResult EthercatMasterController::startCommunication(
    const std::string& nicName,
    const std::string& configFilePath,
    DeviceStatusModel& deviceModel)
{
    if (master_) {
        return {false, configuredSlaveCount_};
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName);

    const SlaveLoadResult loadResult = EthercatSlaveLoader::loadFromFile(configFilePath, *master_, deviceModel);
    configuredSlaveCount_ = loadResult.slaveCount;

    if (!loadResult.ok) {
        reset();
        return {false, configuredSlaveCount_, soem_interface::error::NoError, QStringLiteral("yaml文件错误！请检查格式"), loadResult.errorMessage};
    }

    const auto errorCode = master_->start();
    if (errorCode != soem_interface::error::NoError) {
        const QString logMessage = QStringLiteral("初始化失败，无法连接到网卡 %1").arg(QString::fromStdString(nicName));
        reset();
        return {false, 0, errorCode, logMessage};
    }

    QString slaveCountError;
    if (!validateSlaveCount(configuredSlaveCount_, slaveCountError)) {
        reset();
        return {false, 0, soem_interface::error::InvalidSlave, slaveCountError};
    }

    connected_ = true;
    return {true, configuredSlaveCount_};
}

MasterStartResult EthercatMasterController::enterPreOp(const std::string& nicName)
{
    if (master_) {
        return {false, configuredSlaveCount_};
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName);

    const auto errorCode = master_->initMaster();
    connected_ = (errorCode == soem_interface::error::NoError);
    if (!connected_) {
        reset();
        return {false, 0, errorCode};
    }

    master_->requestPreOp();
    configuredSlaveCount_ = master_->slaveCount();
    return {true, configuredSlaveCount_};
}

MasterStartResult EthercatMasterController::enterMitDebugMode(const std::string& nicName)
{
    if (master_) {
        return {false, configuredSlaveCount_};
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName);

    const SlaveLoadResult loadResult = EthercatSlaveLoader::addMitDebugSlave(*master_);
    configuredSlaveCount_ = loadResult.slaveCount;

    if (!loadResult.ok) {
        reset();
        return {false, configuredSlaveCount_, soem_interface::error::NoError, {}, loadResult.errorMessage};
    }

    const auto errorCode = master_->start();
    if (errorCode != soem_interface::error::NoError) {
        const QString logMessage = QStringLiteral("初始化失败，无法连接到网卡 %1").arg(QString::fromStdString(nicName));
        reset();
        return {false, 0, errorCode, logMessage};
    }

    QString slaveCountError;
    if (!validateSlaveCount(configuredSlaveCount_, slaveCountError)) {
        reset();
        return {false, 0, soem_interface::error::InvalidSlave, slaveCountError};
    }

    connected_ = true;
    return {true, configuredSlaveCount_};
}

} // namespace Backend

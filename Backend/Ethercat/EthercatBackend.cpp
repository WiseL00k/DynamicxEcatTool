#include "EthercatBackend.h"

#include "Backend/Ethercat/EthercatErrorMapper.h"
#include "Backend/Ethercat/SdoConfigMapper.h"
#include "SOEM_interface/EcatMasterBus.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

namespace Backend {

EthercatBackend::EthercatBackend(QObject* parent)
    : QObject(parent)
    , monitorController_(this)
    , flashService_(this)
{
    connectServices();
}

EthercatBackend::~EthercatBackend()
{
    monitorController_.stop();
    masterController_.stop();
}

QStringList EthercatBackend::nicList() const
{
    return nicList_;
}

bool EthercatBackend::connected() const
{
    return connected_;
}

int EthercatBackend::slaveCount() const
{
    return slaveCount_;
}

bool EthercatBackend::setDeviceOnlineStatus(const QString& motorName, const bool& status)
{
    return deviceModel_.setDeviceOnline(motorName, status);
}

void EthercatBackend::connectServices()
{
    connect(&monitorController_, &EthercatMonitorController::logUpdated, this, &EthercatBackend::logUpdated);
    connect(&monitorController_, &EthercatMonitorController::setDeviceOnlineStatus, this, &EthercatBackend::setDeviceOnlineStatus);
    connect(&monitorController_, &EthercatMonitorController::communicationMonitorStopped, this, &EthercatBackend::clearMotorStatusList);

    connect(&flashService_, &FlashService::logUpdated, this, &EthercatBackend::logUpdated);
    connect(&flashService_, &FlashService::errorOccurred, this, &EthercatBackend::soemErrorOccurred);
    connect(&flashService_, &FlashService::flashProgress, this, &EthercatBackend::flashProgress);
    connect(&flashService_, &FlashService::flashFinished, this, &EthercatBackend::flashFinished);
}

bool EthercatBackend::validateSelectedNic() const
{
    return !nicName_.empty();
}

bool EthercatBackend::ensureConfigFileSelected()
{
    if (!configFilePath_.empty()) {
        return true;
    }

    emit logUpdated(QStringLiteral("请先选择yaml配置文件"));
    emit soemErrorOccurred(QStringLiteral("请先选择yaml配置文件"));
    return false;
}

bool EthercatBackend::ensureMonitorStopped()
{
    if (monitorController_.stop()) {
        return true;
    }

    emit soemErrorOccurred(QStringLiteral("监控线程停止超时，请稍后重试"));
    return false;
}

void EthercatBackend::refreshNics()
{
    adapterService_.replaceAdapters(NetworkAdapterService::scan());
    nicList_ = adapterService_.descriptions();
    emit nicListChanged();
}

void EthercatBackend::refreshNicsAsync()
{
    const QPointer<EthercatBackend> self(this);

    QThreadPool::globalInstance()->start([self]() {
        auto adapters = NetworkAdapterService::scan();

        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(
            self,
            [self, adapters = std::move(adapters)]() mutable {
                if (!self) {
                    return;
                }

                self->adapterService_.replaceAdapters(std::move(adapters));
                self->nicList_ = self->adapterService_.descriptions();
                emit self->nicListChanged();
            },
            Qt::QueuedConnection);
    });
}

void EthercatBackend::changedSelectedNic(const int& nicIndex)
{
    QString errorMessage;
    if (!adapterService_.selectAdapter(nicIndex, nicName_, errorMessage)) {
        emit soemErrorOccurred(errorMessage);
        return;
    }

    emit logUpdated(QStringLiteral("选择了网卡: %1").arg(QString::fromStdString(nicName_)));
}

QString EthercatBackend::changeConfigFilePath(const QString& config_file_path)
{
    const QString localPath = QUrl(config_file_path).isLocalFile()
        ? QUrl(config_file_path).toLocalFile()
        : config_file_path;
    configFilePath_ = localPath.toStdString();
    return localPath;
}

void EthercatBackend::updateConnectionState(bool connected, int slaveCount)
{
    connected_ = connected;
    slaveCount_ = slaveCount;

    emit connectedChanged();
    emit slaveCountChanged();
    emit connectedUpdated(connected ? 1 : 0);
}

void EthercatBackend::resetConnectionState()
{
    mode_ = ConnectionMode::Idle;
    updateConnectionState(false, 0);
}

void EthercatBackend::failConnection(soem_interface::error::SoemInterfaceErrorCode errorCode)
{
    masterController_.reset();
    resetConnectionState();
    emit soemErrorOccurred(toUserMessage(errorCode));
}

void EthercatBackend::emitStartFailure(const MasterStartResult& result)
{
    if (!result.logMessage.isEmpty()) {
        emit logUpdated(result.logMessage);
    }

    if (!result.detailMessage.isEmpty()) {
        emit logAppend(result.detailMessage);
    }

    if (result.errorCode != soem_interface::error::NoError) {
        failConnection(result.errorCode);
        return;
    }

    resetConnectionState();
}

void EthercatBackend::startTest()
{
    if (masterController_.hasMaster()) {
        emit soemErrorOccurred(QStringLiteral("当前已有EtherCAT连接"));
        return;
    }

    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    const MasterStartResult result = masterController_.startTest(nicName_);
    if (!result.ok) {
        emitStartFailure(result);
        return;
    }

    mode_ = ConnectionMode::Test;
    updateConnectionState(true, result.slaveCount);
    monitorController_.startTest(masterController_.masterRef());
}

void EthercatBackend::stopTest()
{
    if (!masterController_.hasMaster() || !connected_) {
        return;
    }

    if (!ensureMonitorStopped()) {
        return;
    }

    masterController_.stop();
    resetConnectionState();
}

void EthercatBackend::startCommunication()
{
    if (masterController_.hasMaster()) {
        emit soemErrorOccurred(QStringLiteral("当前已有EtherCAT连接"));
        return;
    }

    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    if (!ensureConfigFileSelected()) {
        return;
    }

    const MasterStartResult result = masterController_.startCommunication(nicName_, configFilePath_, deviceModel_);
    if (!result.ok) {
        emitStartFailure(result);
        return;
    }

    mode_ = ConnectionMode::Communication;
    updateConnectionState(true, result.slaveCount);
    emit motorStatusListChanged();
    monitorController_.startCommunication(masterController_.masterRef());
}

void EthercatBackend::stopCommunication()
{
    if (!masterController_.hasMaster() || !connected_) {
        return;
    }

    if (!ensureMonitorStopped()) {
        return;
    }

    masterController_.stop();
    resetConnectionState();
}

void EthercatBackend::clearMotorStatusList()
{
    deviceModel_.clearAllOnline();
    emit logUpdated(QString());
}

void EthercatBackend::enterPreOpAll()
{
    if (masterController_.hasMaster()) {
        emit soemErrorOccurred(QStringLiteral("当前已有EtherCAT连接"));
        return;
    }

    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    const MasterStartResult result = masterController_.enterPreOp(nicName_);
    if (!result.ok) {
        emitStartFailure(result);
        return;
    }

    mode_ = ConnectionMode::PreOp;
    updateConnectionState(true, result.slaveCount);
}

void EthercatBackend::exitPreOpAll()
{
    if (!masterController_.hasMaster() || !connected_) {
        return;
    }

    if (monitorController_.isRunning()) {
        emit soemErrorOccurred(QStringLiteral("监控线程运行中，无法退出Pre-OP"));
        return;
    }

    masterController_.closePreOp();
    resetConnectionState();
}

void EthercatBackend::enterMitSlaveDebugMode()
{
    if (masterController_.hasMaster()) {
        emit soemErrorOccurred(QStringLiteral("当前已有EtherCAT连接"));
        return;
    }

    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    const MasterStartResult result = masterController_.enterMitDebugMode(nicName_);
    if (!result.ok) {
        emitStartFailure(result);
        return;
    }

    mode_ = ConnectionMode::MitDebug;
    updateConnectionState(true, result.slaveCount);
    monitorController_.startCommunication(masterController_.masterRef());
}

void EthercatBackend::exitMitSlaveDebugMode()
{
    stopCommunication();
}

void EthercatBackend::enableMitSlaveMotors()
{
    QString errorMessage;
    if (!mitSlaveController_.enableMotors(masterController_.master(), errorMessage)) {
        emit soemErrorOccurred(errorMessage);
    }
}

void EthercatBackend::disableMitSlaveMotors()
{
    QString errorMessage;
    if (!mitSlaveController_.disableMotors(masterController_.master(), errorMessage)) {
        emit soemErrorOccurred(errorMessage);
    }
}

bool EthercatBackend::applySDOConfigsQml(const QVariantList& list)
{
    auto* master = masterController_.master();
    if (master == nullptr || !connected_) {
        emit soemErrorOccurred(QStringLiteral("EtherCAT未连接"));
        return false;
    }

    const SdoConfigMappingResult mapping = mapSdoConfigs(list);
    if (!mapping.ok) {
        emit soemErrorOccurred(mapping.errorMessage);
        return false;
    }

    const bool ok = master->applySDOConfigs(mapping.configs);

    emit soemErrorOccurred(ok
        ? QStringLiteral("SDO配置成功!")
        : QStringLiteral("SDO配置失败!请检查参数或从站状态"));

    return ok;
}

void EthercatBackend::flashEEprom(int slaveId, const QString& filePath)
{
    if (connected_) {
        emit soemErrorOccurred(QStringLiteral("请先断开EtherCAT连接再烧录"));
        return;
    }

    flashService_.flashEEprom(nicName_, slaveId, filePath);
}

void EthercatBackend::flashFirmware(int slaveId, const QString& filePath)
{
    if (connected_) {
        emit soemErrorOccurred(QStringLiteral("请先断开EtherCAT连接再烧录"));
        return;
    }

    flashService_.flashFirmware(nicName_, slaveId, filePath);
}

void EthercatBackend::sendMitFrameQml(int canBus, int canId, const QVariantList& data)
{
    QString errorMessage;
    if (!mitSlaveController_.sendFrame(masterController_.master(), canBus, canId, data, errorMessage)) {
        emit soemErrorOccurred(errorMessage);
    }
}

void EthercatBackend::clearMitFrameQml(int canBus, int canId)
{
    QString errorMessage;
    if (!mitSlaveController_.clearFrame(masterController_.master(), canBus, canId, errorMessage)) {
        emit soemErrorOccurred(errorMessage);
    }
}

} // namespace Backend
#include <utility>



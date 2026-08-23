#include "EthercatBackend.h"

#include "Backend/Ethercat/EthercatErrorMapper.h"
#include "Backend/Ethercat/SdoConfigMapper.h"
#include "SOEM_interface/EcatMasterBus.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>
#include <utility>


namespace Backend {

EthercatBackend::EthercatBackend(QObject* parent)
    : QObject(parent)
    , sessionCoordinator_(this)
    , busExplorer_(sessionCoordinator_, masterController_, this)
    , monitorController_(this)
    , flashService_(this)
{
    connectServices();
}

EthercatBackend::~EthercatBackend()
{
    monitorController_.stop();
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

bool EthercatBackend::sessionActive() const
{
    return sessionCoordinator_.active();
}

QString EthercatBackend::sessionMode() const
{
    return sessionCoordinator_.modeName();
}

EthercatExplorerController* EthercatBackend::busExplorer()
{
    return &busExplorer_;
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
    connect(&flashService_, &FlashService::flashFinished, this, [this](const QString& type, bool success, const QString& message) {
        sessionCoordinator_.release(BusSessionCoordinator::Mode::Flashing);
        emit flashFinished(type, success, message);
    });
    connect(&sessionCoordinator_, &BusSessionCoordinator::sessionChanged, this, &EthercatBackend::sessionChanged);
    connect(&busExplorer_, &EthercatExplorerController::errorOccurred, this, &EthercatBackend::soemErrorOccurred);
    connect(&busExplorer_, &EthercatExplorerController::logAppended, this, &EthercatBackend::logAppend);
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

bool EthercatBackend::acquireSession(BusSessionCoordinator::Mode mode)
{
    QString errorMessage;
    if (sessionCoordinator_.tryAcquire(mode, errorMessage)) {
        return true;
    }

    emit soemErrorOccurred(errorMessage);
    return false;
}

bool EthercatBackend::isSession(BusSessionCoordinator::Mode mode) const
{
    return sessionCoordinator_.mode() == mode;
}

void EthercatBackend::refreshNics()
{
    adapterService_.replaceAdapters(NetworkAdapterService::scan());
    nicList_ = adapterService_.descriptions();
    if (nicName_.empty() && !nicList_.isEmpty()) {
        QString errorMessage;
        if (adapterService_.selectAdapter(0, nicName_, errorMessage)) {
            busExplorer_.setNicName(nicName_);
        }
    }
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
                if (self->nicName_.empty() && !self->nicList_.isEmpty()) {
                    QString errorMessage;
                    if (self->adapterService_.selectAdapter(
                            0, self->nicName_, errorMessage)) {
                        self->busExplorer_.setNicName(self->nicName_);
                    }
                }
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

    busExplorer_.setNicName(nicName_);
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

void EthercatBackend::resetConnectionState(BusSessionCoordinator::Mode mode)
{
    sessionCoordinator_.release(mode);
    updateConnectionState(false, 0);
}

void EthercatBackend::failConnection(soem_interface::error::SoemInterfaceErrorCode errorCode, BusSessionCoordinator::Mode mode)
{
    masterController_.reset();
    resetConnectionState(mode);
    emit soemErrorOccurred(toUserMessage(errorCode));
}

void EthercatBackend::emitStartFailure(const MasterStartResult& result, BusSessionCoordinator::Mode mode)
{
    if (!result.logMessage.isEmpty()) {
        emit logUpdated(result.logMessage);
    }

    if (!result.detailMessage.isEmpty()) {
        emit logAppend(result.detailMessage);
    }

    if (result.errorCode != soem_interface::error::NoError) {
        failConnection(result.errorCode, mode);
        return;
    }

    resetConnectionState(mode);
}

void EthercatBackend::startTest()
{
    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    if (!acquireSession(BusSessionCoordinator::Mode::Test)) {
        return;
    }

    const MasterStartResult result = masterController_.startTest(nicName_);
    if (!result.ok) {
        emitStartFailure(result, BusSessionCoordinator::Mode::Test);
        return;
    }

    updateConnectionState(true, result.slaveCount);
    monitorController_.startTest(masterController_.masterRef());
}

void EthercatBackend::stopTest()
{
    if (!isSession(BusSessionCoordinator::Mode::Test) || !masterController_.hasMaster() || !connected_) {
        return;
    }

    if (!ensureMonitorStopped()) {
        return;
    }

    masterController_.stop();
    resetConnectionState(BusSessionCoordinator::Mode::Test);
}

void EthercatBackend::startCommunication()
{
    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    if (!ensureConfigFileSelected()) {
        return;
    }

    if (!acquireSession(BusSessionCoordinator::Mode::Communication)) {
        return;
    }

    const MasterStartResult result = masterController_.startCommunication(nicName_, configFilePath_, deviceModel_);
    if (!result.ok) {
        emitStartFailure(result, BusSessionCoordinator::Mode::Communication);
        return;
    }

    updateConnectionState(true, result.slaveCount);
    emit motorStatusListChanged();
    monitorController_.startCommunication(masterController_.masterRef());
}

void EthercatBackend::stopCommunication()
{
    if (!isSession(BusSessionCoordinator::Mode::Communication) || !masterController_.hasMaster() || !connected_) {
        return;
    }

    if (!ensureMonitorStopped()) {
        return;
    }

    masterController_.stop();
    resetConnectionState(BusSessionCoordinator::Mode::Communication);
}

void EthercatBackend::clearMotorStatusList()
{
    deviceModel_.clearAllOnline();
    emit logUpdated(QString());
}

void EthercatBackend::enterPreOpAll()
{
    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    if (!acquireSession(BusSessionCoordinator::Mode::LegacyPreOp)) {
        return;
    }

    const MasterStartResult result = masterController_.enterPreOp(nicName_);
    if (!result.ok) {
        emitStartFailure(result, BusSessionCoordinator::Mode::LegacyPreOp);
        return;
    }

    updateConnectionState(true, result.slaveCount);
}

void EthercatBackend::exitPreOpAll()
{
    if (!isSession(BusSessionCoordinator::Mode::LegacyPreOp) || !masterController_.hasMaster() || !connected_) {
        return;
    }

    if (monitorController_.isRunning()) {
        emit soemErrorOccurred(QStringLiteral("监控线程运行中，无法退出Pre-OP"));
        return;
    }

    masterController_.closePreOp();
    resetConnectionState(BusSessionCoordinator::Mode::LegacyPreOp);
}

void EthercatBackend::enterMitSlaveDebugMode()
{
    if (!validateSelectedNic()) {
        emit soemErrorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    if (!acquireSession(BusSessionCoordinator::Mode::MitDebug)) {
        return;
    }

    const MasterStartResult result = masterController_.enterMitDebugMode(nicName_);
    if (!result.ok) {
        emitStartFailure(result, BusSessionCoordinator::Mode::MitDebug);
        return;
    }

    updateConnectionState(true, result.slaveCount);
    monitorController_.startCommunication(masterController_.masterRef());
}

void EthercatBackend::exitMitSlaveDebugMode()
{
    if (!isSession(BusSessionCoordinator::Mode::MitDebug) || !masterController_.hasMaster() || !connected_) {
        return;
    }

    if (!ensureMonitorStopped()) {
        return;
    }

    masterController_.stop();
    resetConnectionState(BusSessionCoordinator::Mode::MitDebug);
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
    if (!acquireSession(BusSessionCoordinator::Mode::Flashing)) {
        return;
    }

    if (!flashService_.flashEEprom(nicName_, slaveId, filePath)) {
        sessionCoordinator_.release(BusSessionCoordinator::Mode::Flashing);
    }
}

void EthercatBackend::flashFirmware(int slaveId, const QString& filePath)
{
    if (!acquireSession(BusSessionCoordinator::Mode::Flashing)) {
        return;
    }

    if (!flashService_.flashFirmware(nicName_, slaveId, filePath)) {
        sessionCoordinator_.release(BusSessionCoordinator::Mode::Flashing);
    }
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

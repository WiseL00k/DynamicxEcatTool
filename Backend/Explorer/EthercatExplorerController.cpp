#include "Backend/Explorer/EthercatExplorerController.h"

#include "Backend/Ethercat/BusSessionCoordinator.h"
#include "Backend/Ethercat/EthercatMasterController.h"
#include "Backend/Explorer/PdoValueCodec.h"
#include "SOEM_interface/EcatMasterBus.h"

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace Backend {
namespace {

constexpr int kRefreshIntervalMs = 100;
constexpr int kMaximumLogLines = 500;

bool accessReadable(explorer::AccessMode access)
{
    return access == explorer::AccessMode::ReadOnly
        || access == explorer::AccessMode::ReadWrite;
}

bool accessWritable(explorer::AccessMode access)
{
    return access == explorer::AccessMode::WriteOnly
        || access == explorer::AccessMode::ReadWrite;
}

explorer::PdoDirection toExplorerDirection(soem_interface::PdoDirection direction)
{
    return direction == soem_interface::PdoDirection::Rx
        ? explorer::PdoDirection::Rx
        : explorer::PdoDirection::Tx;
}

soem_interface::PdoDirection toBusDirection(explorer::PdoDirection direction)
{
    return direction == explorer::PdoDirection::Rx
        ? soem_interface::PdoDirection::Rx
        : soem_interface::PdoDirection::Tx;
}

quint32 entryKey(quint16 index, quint8 subIndex)
{
    return (static_cast<quint32>(index) << 8u) | subIndex;
}
bool isPdoConfigurationObject(quint16 index)
{
    return (index >= 0x1600u && index <= 0x1bffu)
        || index == 0x1c12u || index == 0x1c13u;
}


} // namespace

EthercatExplorerController::EthercatExplorerController(
    BusSessionCoordinator& sessionCoordinator,
    EthercatMasterController& masterController,
    QObject* parent)
    : QObject(parent)
    , sessionCoordinator_(sessionCoordinator)
    , masterController_(masterController)
    , slavesModel_(this)
    , pdoEntriesModel_(this)
    , pdoVariableGroupsModel_(this)
    , pdoMappingsModel_(this)
    , objectDictionaryModel_(this)
    , sdoMutex_(std::make_shared<QMutex>())
{
    QSettings settings;
    esiDirectory_ = settings.value(
        QStringLiteral("explorer/esiDirectory"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("esi")))
        .toString();

    refreshTimer_.setInterval(kRefreshIntervalMs);
    workerPool_.setMaxThreadCount(3);
    refreshTimer_.setTimerType(Qt::CoarseTimer);
    connect(&refreshTimer_, &QTimer::timeout,
            this, &EthercatExplorerController::refreshProcessValues);
}

EthercatExplorerController::~EthercatExplorerController()
{
    refreshTimer_.stop();
    workerPool_.clear();
    workerPool_.waitForDone();
    if (sessionCoordinator_.mode() == BusSessionCoordinator::Mode::Explorer) {
        masterController_.stopExplorer();
        sessionCoordinator_.release(BusSessionCoordinator::Mode::Explorer);
    }
}

EthercatExplorerController::Status EthercatExplorerController::status() const
{
    return status_;
}

QString EthercatExplorerController::statusText() const
{
    switch (status_) {
    case Status::Idle: return QStringLiteral("未扫描");
    case Status::Scanning: return QStringLiteral("正在扫描");
    case Status::Ready: return QStringLiteral("扫描完成");
    case Status::Failed: return QStringLiteral("扫描失败");
    case Status::Resetting: return QStringLiteral("正在重置");
    }
    return QStringLiteral("未知");
}

bool EthercatExplorerController::busy() const { return busy_; }
bool EthercatExplorerController::scanned() const { return scanned_; }
int EthercatExplorerController::currentState() const { return currentState_; }
int EthercatExplorerController::slaveCount() const { return slaveCount_; }
bool EthercatExplorerController::mappingReady() const { return mappingReady_; }
bool EthercatExplorerController::allEsiTrusted() const { return allEsiTrusted_; }
QString EthercatExplorerController::esiDirectory() const { return esiDirectory_; }
int EthercatExplorerController::selectedSlaveAddress() const { return selectedSlaveAddress_; }
QStringList EthercatExplorerController::logs() const { return logs_; }

QAbstractItemModel* EthercatExplorerController::slavesModel() { return &slavesModel_; }
QAbstractItemModel* EthercatExplorerController::pdoEntriesModel() { return &pdoEntriesModel_; }
QAbstractItemModel* EthercatExplorerController::pdoVariableGroupsModel()
{
    return &pdoVariableGroupsModel_;
}
QAbstractItemModel* EthercatExplorerController::pdoMappingsModel() { return &pdoMappingsModel_; }
QAbstractItemModel* EthercatExplorerController::objectDictionaryModel() { return &objectDictionaryModel_; }

void EthercatExplorerController::setNicName(const std::string& nicName)
{
    if (!busy_ && !scanned_) {
        nicName_ = nicName;
    }
}

void EthercatExplorerController::setEsiDirectory(const QString& directory)
{
    if (busy_ || scanned_) {
        emit errorOccurred(QStringLiteral("请先重置当前扫描会话，再修改ESI目录"));
        return;
    }
    const QUrl url(directory);
    const QString localPath = QDir::cleanPath(
        url.isLocalFile() ? url.toLocalFile() : directory);
    if (esiDirectory_ == localPath) {
        return;
    }
    esiDirectory_ = localPath;
    QSettings().setValue(QStringLiteral("explorer/esiDirectory"), esiDirectory_);
    emit esiDirectoryChanged();
}

void EthercatExplorerController::setStatus(Status status)
{
    if (status_ != status) {
        status_ = status;
        emit statusChanged();
    }
}

void EthercatExplorerController::setBusy(bool busy)
{
    if (busy_ != busy) {
        busy_ = busy;
        emit busyChanged();
    }
}

void EthercatExplorerController::appendLog(const QString& line)
{
    if (line.isEmpty()) {
        return;
    }
    logs_.push_back(line);
    while (logs_.size() > kMaximumLogLines) {
        logs_.removeFirst();
    }
    emit logsChanged();
    emit logAppended(line);
}

void EthercatExplorerController::scan()
{
    if (busy_) {
        emit errorOccurred(QStringLiteral("总线操作正在进行，请稍后重试"));
        return;
    }
    if (scanned_) {
        emit errorOccurred(QStringLiteral("请先重置当前扫描会话"));
        return;
    }
    if (nicName_.empty()) {
        emit errorOccurred(QStringLiteral("请先选择网卡"));
        return;
    }

    QString sessionError;
    if (!sessionCoordinator_.tryAcquire(
            BusSessionCoordinator::Mode::Explorer, sessionError)) {
        emit errorOccurred(sessionError);
        return;
    }

    clearModels();
    resetPending_ = false;
    const quint64 generation = ++generation_;
    setStatus(Status::Scanning);
    setBusy(true);
    appendLog(QStringLiteral("开始扫描EtherCAT总线"));

    const std::string nicName = nicName_;
    const QString esiDirectory = esiDirectory_;
    const QPointer<EthercatExplorerController> self(this);
    workerPool_.start([self, generation, nicName, esiDirectory]() mutable {
        explorer::EsiRepository localRepository;
        const explorer::EsiRepositoryIndexResult indexResult =
            localRepository.indexDirectory(esiDirectory);
        QVector<explorer::EsiDevice> devices = localRepository.devices();
        QVector<explorer::ParseDiagnostic> diagnostics = indexResult.diagnostics;
        if (!self) {
            return;
        }
        soem_interface::BusScanResult scanResult =
            self->masterController_.startExplorer(nicName);
        QMetaObject::invokeMethod(
            self,
            [self, generation, result = std::move(scanResult),
             devices = std::move(devices),
             diagnostics = std::move(diagnostics)]() mutable {
                if (self) {
                    self->completeScan(generation, std::move(result),
                                       std::move(devices), std::move(diagnostics));
                }
            },
            Qt::QueuedConnection);
    });
}

void EthercatExplorerController::completeScan(
    quint64 generation,
    soem_interface::BusScanResult result,
    QVector<explorer::EsiDevice> devices,
    QVector<explorer::ParseDiagnostic> diagnostics)
{
    if (generation != generation_ || resetPending_) {
        performReset();
        return;
    }

    for (const explorer::ParseDiagnostic& diagnostic : std::as_const(diagnostics)) {
        appendLog(QStringLiteral("ESI%1: %2 (%3:%4)")
                      .arg(diagnostic.severity == explorer::ParseDiagnostic::Severity::Error
                               ? QStringLiteral("错误") : QStringLiteral("警告"),
                           diagnostic.message, diagnostic.source)
                      .arg(diagnostic.line));
    }

    if (!result.success) {
        setBusy(false);
        setStatus(Status::Failed);
        sessionCoordinator_.release(BusSessionCoordinator::Mode::Explorer);
        const QString message = result.error.empty()
            ? QStringLiteral("EtherCAT总线扫描失败")
            : QString::fromStdString(result.error);
        appendLog(message);
        emit errorOccurred(message);
        emit sessionReleased();
        return;
    }

    repository_.replaceDevices(std::move(devices));
    buildRuntime(result);
    scanned_ = true;
    currentState_ = EC_STATE_PRE_OP;
    slaveCount_ = result.slaveCount;
    mappingReady_ = result.mappingReady;
    setBusy(false);
    setStatus(Status::Ready);
    emit scannedChanged();
    emit currentStateChanged();
    emit slaveCountChanged();
    emit mappingReadyChanged();
    emit allEsiTrustedChanged();

    appendLog(QStringLiteral("扫描完成，共发现 %1 个从站").arg(slaveCount_));
    appendLog(allEsiTrusted_
                  ? QStringLiteral("全部从站ESI与活动PDO映射可信")
                  : QStringLiteral("存在ESI缺失或映射不一致，已禁止进入OP"));
    if (selectedSlaveAddress_ > 0) {
        loadOnlineObjectDictionary(static_cast<quint16>(selectedSlaveAddress_));
    }
}

void EthercatExplorerController::buildRuntime(
    const soem_interface::BusScanResult& result)
{
    runtimes_.clear();
    const int previousSelection = selectedSlaveAddress_;
    QVector<explorer::SlaveSnapshot> snapshots;
    allEsiTrusted_ = !result.slaves.empty();

    for (const soem_interface::SlaveIdentitySnapshot& identity : result.slaves) {
        explorer::OnlineSlaveIdentity online;
        online.address = identity.position;
        online.name = QString::fromStdString(identity.name);
        online.vendorId = identity.vendorId;
        online.productCode = identity.productCode;
        online.revisionNo = identity.revision;

        int outputBits = 0;
        int inputBits = 0;
        for (const soem_interface::ActivePdoEntry& entry : result.activePdos) {
            if (entry.slave != identity.position) {
                continue;
            }
            online.activePdoEntries.push_back({
                toExplorerDirection(entry.direction),
                entry.index,
                entry.subindex,
                entry.bitLength});
            if (entry.direction == soem_interface::PdoDirection::Rx) {
                outputBits += entry.bitLength;
            } else {
                inputBits += entry.bitLength;
            }
        }

        online.activeMappingKnown =
            outputBits == identity.outputBits && inputBits == identity.inputBits;

        SlaveRuntime runtime;
        runtime.match = repository_.match(online);
        if (!online.activeMappingKnown) {
            runtime.match.trusted = false;
            runtime.match.reason = QStringLiteral(
                "活动PDO条目无法完整覆盖SOEM过程映像");
        }
        runtime.mappings = mergeMappings(identity.position, result.activePdos, runtime.match);
        runtime.objects = runtime.match.matched
            ? flattenedObjects(runtime.match.device.objects)
            : QVector<explorer::ObjectDictionaryEntry>{};
        runtime.outputBits = identity.outputBits;
        runtime.inputBits = identity.inputBits;
        allEsiTrusted_ = allEsiTrusted_ && runtime.match.trusted;
        runtimes_.insert(identity.position, runtime);

        explorer::SlaveSnapshot snapshot;
        snapshot.address = identity.position;
        snapshot.name = QString::fromStdString(identity.name);
        snapshot.vendorId = identity.vendorId;
        snapshot.productCode = identity.productCode;
        snapshot.revisionNo = identity.revision;
        snapshot.serialNumber = identity.serial.has_value()
            ? explorer::hexValue(*identity.serial, 8) : QStringLiteral("-");
        snapshot.state = identity.state;
        snapshot.stateText = stateText(identity.state);
        snapshot.alStatusCode = identity.alStatusCode;
        snapshot.alStatusText = QString::fromStdString(identity.alStatusText);
        snapshot.inputBits = identity.inputBits;
        snapshot.outputBits = identity.outputBits;
        snapshot.esiMatched = runtime.match.matched;
        snapshot.esiTrusted = runtime.match.trusted;
        snapshot.esiPath = runtime.match.matched
            ? runtime.match.device.sourceFile : runtime.match.reason;
        snapshots.push_back(std::move(snapshot));

        appendLog(QStringLiteral("从站 %1: %2, ESI: %3")
                      .arg(identity.position)
                      .arg(QString::fromStdString(identity.name))
                      .arg(runtime.match.reason));
    }

    slavesModel_.setItems(std::move(snapshots));
    selectedSlaveAddress_ = runtimes_.contains(previousSelection)
        ? previousSelection
        : (result.slaves.empty() ? 0 : result.slaves.front().position);
    emit selectedSlaveAddressChanged();
    refreshSelectedModels();
}

QVector<explorer::PdoMapping> EthercatExplorerController::mergeMappings(
    quint16 address,
    const std::vector<soem_interface::ActivePdoEntry>& activeEntries,
    const explorer::EsiMatchResult& match)
{
    QVector<explorer::PdoMapping> mappings;
    for (const soem_interface::ActivePdoEntry& active : activeEntries) {
        if (active.slave != address) {
            continue;
        }
        const explorer::PdoDirection direction = toExplorerDirection(active.direction);
        explorer::PdoMapping* mapping = nullptr;
        for (explorer::PdoMapping& candidate : mappings) {
            if (candidate.direction == direction && candidate.index == active.pdoIndex) {
                mapping = &candidate;
                break;
            }
        }

        const explorer::PdoMapping* esiMapping = nullptr;
        if (match.matched) {
            for (const explorer::PdoMapping& candidate : match.device.pdoMappings) {
                if (candidate.direction == direction && candidate.index == active.pdoIndex) {
                    esiMapping = &candidate;
                    break;
                }
            }
        }
        if (mapping == nullptr) {
            explorer::PdoMapping created;
            created.direction = direction;
            created.index = active.pdoIndex;
            created.name = esiMapping != nullptr ? esiMapping->name
                : QStringLiteral("PDO %1").arg(explorer::hexValue(active.pdoIndex, 4));
            created.syncManager = esiMapping != nullptr ? esiMapping->syncManager : -1;
            created.fixed = esiMapping != nullptr && esiMapping->fixed;
            created.mandatory = esiMapping != nullptr && esiMapping->mandatory;
            mappings.push_back(std::move(created));
            mapping = &mappings.last();
        }

        const explorer::PdoEntry* esiEntry = nullptr;
        if (esiMapping != nullptr) {
            for (const explorer::PdoEntry& candidate : esiMapping->entries) {
                if (candidate.index == active.index && candidate.subIndex == active.subindex) {
                    esiEntry = &candidate;
                    break;
                }
            }
        }

        explorer::PdoEntry entry;
        entry.index = active.index;
        entry.subIndex = active.subindex;
        entry.name = esiEntry != nullptr && !esiEntry->name.isEmpty()
            ? esiEntry->name
            : (!active.name.empty() ? QString::fromStdString(active.name)
                                    : QStringLiteral("%1:%2")
                                          .arg(explorer::hexValue(active.index, 4),
                                               explorer::hexValue(active.subindex, 2)));
        entry.dataType = esiEntry != nullptr ? esiEntry->dataType
                                             : coeDataTypeName(active.dataType, active.bitLength);
        entry.bitLength = active.bitLength;
        entry.pdoBitOffset = mapping->bitLength;
        entry.processBitOffset = active.bitOffset;
        entry.arrayName = esiEntry != nullptr ? esiEntry->arrayName : QString{};
        entry.arrayLowerBound = esiEntry != nullptr ? esiEntry->arrayLowerBound : 0;
        entry.arrayElements = esiEntry != nullptr ? esiEntry->arrayElements : 0;
        entry.arrayElementIndex = esiEntry != nullptr ? esiEntry->arrayElementIndex : -1;
        mapping->entries.push_back(std::move(entry));
        mapping->bitLength += active.bitLength;
    }
    return mappings;
}

QVector<explorer::ObjectDictionaryEntry>
EthercatExplorerController::flattenedObjects(
    const QVector<explorer::ObjectDictionaryEntry>& objects)
{
    QVector<explorer::ObjectDictionaryEntry> flattened;
    for (const explorer::ObjectDictionaryEntry& object : objects) {
        if (object.subItems.isEmpty()) {
            flattened.push_back(object);
            continue;
        }
        for (const explorer::OdSubItem& subItem : object.subItems) {
            explorer::ObjectDictionaryEntry item;
            item.index = object.index;
            item.name = subItem.name.isEmpty() ? object.name : subItem.name;
            item.dataType = subItem.dataType;
            item.bitSize = subItem.bitSize;
            item.access = subItem.access == explorer::AccessMode::None
                ? object.access : subItem.access;
            item.pdoMapping = subItem.pdoMapping.isEmpty()
                ? object.pdoMapping : subItem.pdoMapping;
            item.defaultValue = subItem.defaultValue;
            explorer::OdSubItem encoded = subItem;
            item.onlineAccessMask = object.onlineAccessMask;
            item.onlineAccessKnown = object.onlineAccessKnown;
            encoded.name = item.name;
            item.subItems = {encoded};
            flattened.push_back(std::move(item));
        }
    }
    return flattened;
}

void EthercatExplorerController::refreshSelectedModels()
{
    const auto runtimeIt = runtimes_.constFind(
        static_cast<quint16>(selectedSlaveAddress_));
    if (runtimeIt == runtimes_.constEnd()) {
        pdoEntriesModel_.clear();
        pdoVariableGroupsModel_.clear();
        pdoMappingsModel_.clear();
        objectDictionaryModel_.clear();
        return;
    }

    pdoMappingsModel_.setMappings(
        static_cast<quint16>(selectedSlaveAddress_), runtimeIt->mappings);
    objectDictionaryModel_.setObjects(
        static_cast<quint16>(selectedSlaveAddress_), runtimeIt->objects);

    QVector<explorer::PdoVariable> variables;
    for (const explorer::PdoMapping& mapping : runtimeIt->mappings) {
        for (const explorer::PdoEntry& entry : mapping.entries) {
            explorer::PdoVariable variable;
            variable.stableId = explorer::makePdoStableId(
                static_cast<quint16>(selectedSlaveAddress_), mapping.direction,
                mapping.index, entry.index, entry.subIndex);
            variable.stableId += QStringLiteral(":%1").arg(entry.processBitOffset);
            variable.slaveAddress = static_cast<quint16>(selectedSlaveAddress_);
            variable.direction = mapping.direction;
            variable.pdoIndex = mapping.index;
            variable.pdoName = mapping.name;
            variable.index = entry.index;
            variable.subIndex = entry.subIndex;
            variable.name = entry.name;
            variable.dataType = entry.dataType;
            variable.bitLength = entry.bitLength;
            variable.bitOffset = entry.processBitOffset;
            variable.arrayName = entry.arrayName;
            variable.arrayLowerBound = entry.arrayLowerBound;
            variable.arrayElements = entry.arrayElements;
            variable.arrayElementIndex = entry.arrayElementIndex;
            variable.writable = runtimeIt->match.trusted
                && mapping.direction == explorer::PdoDirection::Rx;
            variables.push_back(std::move(variable));
        }
    }
    pdoEntriesModel_.setItems(variables);
    pdoVariableGroupsModel_.setVariables(
        std::move(variables), runtimeIt->match.trusted);
    refreshProcessValues();
}

void EthercatExplorerController::applyOnlineAccessForState()
{
    for (auto runtime = runtimes_.begin(); runtime != runtimes_.end(); ++runtime) {
        for (explorer::ObjectDictionaryEntry& object : runtime->objects) {
            if (!object.onlineAccessKnown) {
                continue;
            }
            explorer::AccessMode access =
                accessModeFromCoe(object.onlineAccessMask, currentState_);
            if (isPdoConfigurationObject(object.index) && accessWritable(access)) {
                access = accessReadable(access)
                    ? explorer::AccessMode::ReadOnly : explorer::AccessMode::None;
            }
            object.access = access;
            if (!object.subItems.isEmpty()) {
                object.subItems.first().access = access;
            }
        }
    }
    refreshSelectedModels();
}

void EthercatExplorerController::selectSlave(int address)
{
    if (!scanned_ || busy_ || address <= 0 || !runtimes_.contains(address)
        || selectedSlaveAddress_ == address) {
        return;
    }
    selectedSlaveAddress_ = address;
    emit selectedSlaveAddressChanged();
    refreshSelectedModels();
    loadOnlineObjectDictionary(static_cast<quint16>(address));
}

void EthercatExplorerController::selectPdoArrayElement(
    const QString& groupId, int elementIndex)
{
    if (!pdoVariableGroupsModel_.selectElement(groupId, elementIndex)) {
        emit errorOccurred(QStringLiteral("PDO数组元素选择无效"));
    }
}

bool EthercatExplorerController::validateStateRequest(
    int state, QString& errorMessage) const
{
    if (!scanned_) {
        errorMessage = QStringLiteral("请先扫描总线");
    } else if (busy_) {
        errorMessage = QStringLiteral("总线操作正在进行");
    } else if (state != Init && state != PreOp && state != SafeOp && state != Op) {
        errorMessage = QStringLiteral("不支持的EtherCAT状态");
    } else if (state == Op && !mappingReady_) {
        errorMessage = QStringLiteral("PDO过程映像尚未建立");
    } else if (state == Op && !allEsiTrusted_) {
        errorMessage = QStringLiteral("存在不可信的ESI或PDO映射，禁止进入OP");
    }
    return errorMessage.isEmpty();
}

void EthercatExplorerController::requestState(int state)
{
    QString errorMessage;
    if (!validateStateRequest(state, errorMessage)) {
        emit errorOccurred(errorMessage);
        return;
    }
    const auto master = masterController_.masterRef();
    if (!master) {
        emit errorOccurred(QStringLiteral("EtherCAT主站会话不存在"));
        return;
    }

    const quint64 generation = generation_;
    setBusy(true);
    appendLog(QStringLiteral("请求全站进入 %1").arg(stateText(state)));
    const QPointer<EthercatExplorerController> self(this);
    workerPool_.start([self, master, generation, state]() {
        soem_interface::BusStateResult result =
            master->requestStateDetailed(static_cast<quint16>(state));
        QMetaObject::invokeMethod(
            self,
            [self, generation, state, result = std::move(result)]() mutable {
                if (self) {
                    self->completeStateRequest(generation, state, std::move(result));
                }
            },
            Qt::QueuedConnection);
    });
}

void EthercatExplorerController::completeStateRequest(
    quint64 generation, int requestedState, soem_interface::BusStateResult result)
{
    if (generation != generation_ || resetPending_) {
        performReset();
        return;
    }

    const auto master = masterController_.masterRef();
    const int actualState = result.actualState & 0x0f;
    if ((actualState == Init || actualState == PreOp
         || actualState == SafeOp || actualState == Op)
        && currentState_ != actualState) {
        currentState_ = actualState;
        emit currentStateChanged();
    }

    const bool ready = master && master->isMappingReady();
    if (mappingReady_ != ready) {
        mappingReady_ = ready;
        emit mappingReadyChanged();
    }

    bool runtimeRebuilt = false;
    if (result.success && requestedState == SafeOp && ready && master) {
        soem_interface::BusScanResult snapshot;
        snapshot.success = true;
        snapshot.slaveCount = master->slaveCount();
        snapshot.ioMapSize = master->ioMapSize();
        snapshot.expectedWorkingCounter = master->expectedWorkingCounter();
        snapshot.mappingReady = true;
        snapshot.slaves = master->slaveIdentities();
        snapshot.activePdos = master->activePdoMappings();
        buildRuntime(snapshot);
        runtimeRebuilt = true;
        emit allEsiTrustedChanged();
        appendLog(QStringLiteral("SAFE-OP映射已重新读取并完成ESI信任校验"));
    } else if (currentState_ == Init) {
        for (auto runtime = runtimes_.begin(); runtime != runtimes_.end(); ++runtime) {
            runtime->match.trusted = false;
            runtime->match.reason = QStringLiteral("INIT后活动PDO映射需要重新确认");
        }
        allEsiTrusted_ = false;
        emit allEsiTrustedChanged();
    }

    updateSlaveStates(result);
    setBusy(false);
    applyOnlineAccessForState();

    if (currentState_ == Op && result.success) {
        wkcFaultReported_ = false;
        refreshTimer_.start();
    } else {
        refreshTimer_.stop();
    }

    if (runtimeRebuilt && selectedSlaveAddress_ > 0) {
        loadOnlineObjectDictionary(static_cast<quint16>(selectedSlaveAddress_));
    }

    if (!result.success) {
        const QString message = result.error.empty()
            ? QStringLiteral("部分从站未能进入 %1").arg(stateText(requestedState))
            : QString::fromStdString(result.error);
        appendLog(message);
        emit errorOccurred(message);
        return;
    }

    appendLog(QStringLiteral("全部从站已进入 %1").arg(stateText(requestedState)));
}

void EthercatExplorerController::updateSlaveStates(
    const soem_interface::BusStateResult& result)
{
    QVector<explorer::SlaveSnapshot> snapshots = slavesModel_.items();
    for (const soem_interface::SlaveStateResult& state : result.slaves) {
        for (explorer::SlaveSnapshot& snapshot : snapshots) {
            if (snapshot.address == state.position) {
                snapshot.state = state.actualState;
                snapshot.stateText = stateText(state.actualState);
                snapshot.alStatusCode = state.alStatusCode;
                snapshot.alStatusText = QString::fromStdString(state.alStatusText);
                break;
            }
        }
    }
    slavesModel_.setItems(std::move(snapshots));
}

void EthercatExplorerController::writePdoValue(
    const QString& stableId, const QVariant& value)
{
    if (!scanned_ || busy_ || wkcFaultReported_ || currentState_ != Op) {
        emit errorOccurred(QStringLiteral("PDO输出只能在OP状态下修改"));
        return;
    }

    const explorer::PdoVariable* variable =
        pdoEntriesModel_.itemByStableId(stableId);
    if (variable == nullptr || !variable->writable
        || variable->direction != explorer::PdoDirection::Rx) {
        emit errorOccurred(QStringLiteral("该PDO变量不可写"));
        return;
    }

    const auto master = masterController_.masterRef();
    if (master == nullptr) {
        emit errorOccurred(QStringLiteral("EtherCAT主站会话不存在"));
        return;
    }

    const qsizetype firstByte = variable->bitOffset / 8;
    const qsizetype relativeBit = variable->bitOffset - firstByte * 8;
    const qsizetype byteCount =
        (relativeBit + variable->bitLength + 7) / 8;
    std::vector<uint8_t> current;
    if (!master->readProcessDataRange(
            soem_interface::PdoDirection::Rx, variable->slaveAddress,
            static_cast<size_t>(firstByte), static_cast<size_t>(byteCount), current)) {
        emit errorOccurred(QStringLiteral("读取PDO输出缓存失败"));
        return;
    }

    QByteArray image(reinterpret_cast<const char*>(current.data()),
                     static_cast<qsizetype>(current.size()));
    const explorer::PdoEncodeResult encoded = explorer::PdoValueCodec::encode(
        value, variable->dataType, variable->bitLength, &image, relativeBit);
    if (!encoded.ok) {
        emit errorOccurred(encoded.error);
        return;
    }

    std::vector<uint8_t> bytes(
        reinterpret_cast<const uint8_t*>(image.constData()),
        reinterpret_cast<const uint8_t*>(image.constData()) + image.size());
    if (!master->writeProcessDataRange(
            variable->slaveAddress, static_cast<size_t>(firstByte), bytes)) {
        emit errorOccurred(QStringLiteral("PDO输出写入队列失败"));
        return;
    }

    const explorer::PdoDecodeResult decoded = explorer::PdoValueCodec::decode(
        image, relativeBit, variable->bitLength, variable->dataType);
    pdoEntriesModel_.updateValue(
        stableId, decoded.ok ? decoded.value : value,
        decoded.ok ? decoded.displayValue : value.toString());
    pdoVariableGroupsModel_.updateValue(
        stableId, decoded.ok ? decoded.value : value,
        decoded.ok ? decoded.displayValue : value.toString());
}

void EthercatExplorerController::refreshProcessValues()
{
    if (!scanned_ || busy_ || currentState_ != Op || selectedSlaveAddress_ <= 0) {
        return;
    }

    const auto master = masterController_.masterRef();
    const auto runtimeIt = runtimes_.constFind(
        static_cast<quint16>(selectedSlaveAddress_));
    if (master == nullptr || runtimeIt == runtimes_.constEnd()) {
        return;
    }
    const soem_interface::ProcessDataSnapshot processSnapshot =
        master->processDataSnapshot();
    const int expectedWkc = master->expectedWorkingCounter();
    if (expectedWkc > 0 && processSnapshot.workingCounter < expectedWkc) {
        if (!wkcFaultReported_) {
            wkcFaultReported_ = true;
            const QString message = QStringLiteral(
                "过程数据WKC异常: 当前 %1，期望至少 %2")
                    .arg(processSnapshot.workingCounter)
                    .arg(expectedWkc);
            appendLog(message);
            emit errorOccurred(message);
        }

        const soem_interface::BusStateResult state = master->stateSnapshot();
        updateSlaveStates(state);
        const int actualState = state.actualState & 0x0f;
        if ((actualState == Init || actualState == PreOp
             || actualState == SafeOp || actualState == Op)
            && currentState_ != actualState) {
            currentState_ = actualState;
            emit currentStateChanged();
            if (currentState_ != Op) {
                refreshTimer_.stop();
                applyOnlineAccessForState();
            }
        }
        return;
    }
    if (wkcFaultReported_) {
        wkcFaultReported_ = false;
        appendLog(QStringLiteral("过程数据WKC已恢复"));
    }

    QHash<int, QByteArray> images;
    for (explorer::PdoDirection direction :
         {explorer::PdoDirection::Rx, explorer::PdoDirection::Tx}) {
        const int bitCount = direction == explorer::PdoDirection::Rx
            ? runtimeIt->outputBits : runtimeIt->inputBits;
        const size_t byteCount = static_cast<size_t>((bitCount + 7) / 8);
        if (byteCount == 0) {
            continue;
        }
        std::vector<uint8_t> bytes;
        if (!master->readProcessDataRange(
                toBusDirection(direction),
                static_cast<quint16>(selectedSlaveAddress_),
                0, byteCount, bytes)) {
            continue;
        }
        images.insert(static_cast<int>(direction),
                      QByteArray(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<qsizetype>(bytes.size())));
    }

    const QVector<explorer::PdoVariable> variables = pdoEntriesModel_.items();
    for (const explorer::PdoVariable& variable : variables) {
        const QByteArray image = images.value(static_cast<int>(variable.direction));
        if (image.isEmpty()) {
            continue;
        }
        const explorer::PdoDecodeResult decoded = explorer::PdoValueCodec::decode(
            image, variable.bitOffset, variable.bitLength, variable.dataType);
        if (decoded.ok) {
            pdoEntriesModel_.updateValue(
                variable.stableId, decoded.value, decoded.displayValue);
            pdoVariableGroupsModel_.updateValue(
                variable.stableId, decoded.value, decoded.displayValue);
        }
    }
}

void EthercatExplorerController::loadOnlineObjectDictionary(quint16 address)
{
    const auto master = masterController_.masterRef();
    if (!master || !scanned_) {
        return;
    }

    const quint64 generation = generation_;
    const QPointer<EthercatExplorerController> self(this);
    const std::shared_ptr<QMutex> mutex = sdoMutex_;
    workerPool_.start([self, master, mutex, generation, address]() {
        QMutexLocker locker(mutex.get());
        soem_interface::OnlineOdResult result =
            master->readOnlineObjectDictionary(address);
        QMetaObject::invokeMethod(
            self,
            [self, generation, address, result = std::move(result)]() mutable {
                if (!self || generation != self->generation_ || !self->scanned_) {
                    return;
                }
                auto runtimeIt = self->runtimes_.find(address);
                if (runtimeIt == self->runtimes_.end()) {
                    return;
                }
                if (!result.success) {
                    self->appendLog(QStringLiteral("从站%1在线对象字典读取失败: %2")
                                        .arg(address)
                                        .arg(QString::fromStdString(result.error)));
                    return;
                }

                QHash<quint32, int> positions;
                for (int i = 0; i < runtimeIt->objects.size(); ++i) {
                    const auto& object = runtimeIt->objects.at(i);
                    const quint8 subIndex = object.subItems.isEmpty()
                        ? 0 : object.subItems.first().subIndex;
                    positions.insert(entryKey(object.index, subIndex), i);
                }

                for (const soem_interface::OnlineOdEntry& online : result.entries) {
                    explorer::ObjectDictionaryEntry object;
                    const auto existing = positions.constFind(
                        entryKey(online.index, online.subindex));
                    if (existing != positions.constEnd()) {
                        object = runtimeIt->objects.at(existing.value());
                    }
                    object.index = online.index;
                    if (!online.name.empty()) {
                        object.name = QString::fromStdString(online.name);
                    }
                    object.dataType = coeDataTypeName(online.dataType, online.bitLength);
                    object.bitSize = online.bitLength;
                    object.onlineAccessMask = online.objectAccess;
                    object.onlineAccessKnown = true;
                    object.access = accessModeFromCoe(
                        online.objectAccess, self->currentState_);
                    if (isPdoConfigurationObject(object.index)
                        && accessWritable(object.access)) {
                        object.access = accessReadable(object.access)
                            ? explorer::AccessMode::ReadOnly : explorer::AccessMode::None;
                    }
                    explorer::OdSubItem sub;
                    sub.subIndex = online.subindex;
                    sub.name = object.name;
                    sub.dataType = object.dataType;
                    sub.bitSize = object.bitSize;
                    sub.access = object.access;
                    object.subItems = {sub};

                    if (existing == positions.constEnd()) {
                        positions.insert(entryKey(online.index, online.subindex),
                                         runtimeIt->objects.size());
                        runtimeIt->objects.push_back(std::move(object));
                    } else {
                        runtimeIt->objects[existing.value()] = std::move(object);
                    }
                }

                if (self->selectedSlaveAddress_ == address) {
                    self->objectDictionaryModel_.setObjects(
                        address, runtimeIt->objects);
                }
                self->appendLog(QStringLiteral("从站%1在线对象字典读取完成，共%2项")
                                    .arg(address)
                                    .arg(result.entries.size()));
            },
            Qt::QueuedConnection);
    });
}

void EthercatExplorerController::readSdoValue(const QString& stableId)
{
    const explorer::ObjectDictionaryItem* item =
        objectDictionaryModel_.itemByStableId(stableId);
    if (!scanned_ || busy_ || item == nullptr || !accessReadable(item->access)) {
        emit errorOccurred(QStringLiteral("该对象不可读"));
        return;
    }

    const explorer::ObjectDictionaryItem request = *item;
    const auto master = masterController_.masterRef();
    if (!master) {
        emit errorOccurred(QStringLiteral("EtherCAT主站会话不存在"));
        return;
    }

    const int byteCount = qMax(1, (request.bitLength + 7) / 8);
    const quint64 generation = generation_;
    const QPointer<EthercatExplorerController> self(this);
    const std::shared_ptr<QMutex> mutex = sdoMutex_;
    workerPool_.start(
        [self, master, mutex, request, byteCount, generation]() {
            QMutexLocker locker(mutex.get());
            QByteArray bytes(byteCount, char(0));
            const bool ok = master->sdoRead(
                request.slaveAddress, request.index, request.subIndex,
                false, byteCount, bytes.data());
            explorer::PdoDecodeResult decoded;
            if (ok) {
                decoded = explorer::PdoValueCodec::decode(
                    bytes, 0, request.bitLength, request.dataType);
            }
            QMetaObject::invokeMethod(
                self,
                [self, generation, request, ok, decoded]() {
                    if (!self || generation != self->generation_) {
                        return;
                    }
                    if (!ok || !decoded.ok) {
                        emit self->errorOccurred(
                            !ok ? QStringLiteral("SDO读取失败") : decoded.error);
                        return;
                    }
                    self->objectDictionaryModel_.updateValue(
                        request.stableId, decoded.value, decoded.displayValue);
                },
                Qt::QueuedConnection);
        });
}

void EthercatExplorerController::writeSdoValue(
    const QString& stableId, const QVariant& value)
{
    const explorer::ObjectDictionaryItem* item =
        objectDictionaryModel_.itemByStableId(stableId);
    if (!scanned_ || busy_ || item == nullptr || !accessWritable(item->access)
        || isPdoConfigurationObject(item->index)) {
        emit errorOccurred(QStringLiteral("该对象在当前权限下不可写"));
        return;
    }

    const explorer::ObjectDictionaryItem request = *item;
    const int byteCount = qMax(1, (request.bitLength + 7) / 8);
    QByteArray bytes(byteCount, char(0));
    const explorer::PdoEncodeResult encoded = explorer::PdoValueCodec::encode(
        value, request.dataType, request.bitLength, &bytes, 0);
    if (!encoded.ok) {
        emit errorOccurred(encoded.error);
        return;
    }

    const auto master = masterController_.masterRef();
    if (!master) {
        emit errorOccurred(QStringLiteral("EtherCAT主站会话不存在"));
        return;
    }

    const quint64 generation = generation_;
    const QPointer<EthercatExplorerController> self(this);
    const std::shared_ptr<QMutex> mutex = sdoMutex_;
    workerPool_.start(
        [self, master, mutex, request, bytes, value, generation]() mutable {
            QMutexLocker locker(mutex.get());
            const bool ok = master->sdoWrite(
                request.slaveAddress, request.index, request.subIndex,
                false, bytes.size(), bytes.data());
            QMetaObject::invokeMethod(
                self,
                [self, generation, request, value, ok]() {
                    if (!self || generation != self->generation_) {
                        return;
                    }
                    if (!ok) {
                        emit self->errorOccurred(QStringLiteral("SDO写入失败"));
                        return;
                    }
                    self->objectDictionaryModel_.updateValue(
                        request.stableId, value, value.toString());
                },
                Qt::QueuedConnection);
        });
}

void EthercatExplorerController::reset()
{
    if (busy_) {
        if (status_ == Status::Resetting || resetPending_) {
            return;
        }
        resetPending_ = true;
        ++generation_;
        setStatus(Status::Resetting);
        refreshTimer_.stop();
        return;
    }
    performReset();
}

void EthercatExplorerController::performReset()
{
    resetPending_ = false;
    refreshTimer_.stop();
    const quint64 generation = ++generation_;
    setStatus(Status::Resetting);
    setBusy(true);

    if (sessionCoordinator_.mode() != BusSessionCoordinator::Mode::Explorer) {
        completeReset(generation);
        return;
    }

    const QPointer<EthercatExplorerController> self(this);
    const auto master = masterController_.masterRef();
    workerPool_.start([self, master, generation]() {
        if (!self) {
            return;
        }
        if (master) {
            master->resetExplorer();
        }
        QMetaObject::invokeMethod(
            self,
            [self, generation]() {
                if (self) {
                    self->completeReset(generation);
                }
            },
            Qt::QueuedConnection);
    });
}

void EthercatExplorerController::completeReset(quint64 generation)
{
    if (generation != generation_) {
        return;
    }
    if (sessionCoordinator_.mode() == BusSessionCoordinator::Mode::Explorer) {
        masterController_.reset();
    }
    sessionCoordinator_.release(BusSessionCoordinator::Mode::Explorer);
    clearModels();
    scanned_ = false;
    currentState_ = 0;
    slaveCount_ = 0;
    mappingReady_ = false;
    allEsiTrusted_ = false;
    wkcFaultReported_ = false;
    setBusy(false);
    setStatus(Status::Idle);
    emit scannedChanged();
    emit currentStateChanged();
    emit slaveCountChanged();
    emit mappingReadyChanged();
    emit allEsiTrustedChanged();
    emit sessionReleased();
}

void EthercatExplorerController::clearModels()
{
    slavesModel_.clear();
    pdoEntriesModel_.clear();
    pdoVariableGroupsModel_.clear();
    pdoMappingsModel_.clear();
    objectDictionaryModel_.clear();
    repository_.clear();
    runtimes_.clear();
    if (selectedSlaveAddress_ != 0) {
        selectedSlaveAddress_ = 0;
        emit selectedSlaveAddressChanged();
    }
    logs_.clear();
    emit logsChanged();
}

QString EthercatExplorerController::stateText(int state)
{
    switch (state & 0x0f) {
    case EC_STATE_INIT: return QStringLiteral("INIT");
    case EC_STATE_PRE_OP: return QStringLiteral("PRE-OP");
    case EC_STATE_SAFE_OP: return QStringLiteral("SAFE-OP");
    case EC_STATE_OPERATIONAL: return QStringLiteral("OP");
    default: return QStringLiteral("UNKNOWN");
    }
}

QString EthercatExplorerController::coeDataTypeName(
    quint16 dataType, int bitLength)
{
    switch (dataType) {
    case ECT_BOOLEAN: return QStringLiteral("BOOL");
    case ECT_INTEGER8: return QStringLiteral("INTEGER8");
    case ECT_INTEGER16: return QStringLiteral("INTEGER16");
    case ECT_INTEGER24: return QStringLiteral("INTEGER24");
    case ECT_INTEGER32: return QStringLiteral("INTEGER32");
    case ECT_INTEGER64: return QStringLiteral("INTEGER64");
    case ECT_UNSIGNED8: return QStringLiteral("UNSIGNED8");
    case ECT_UNSIGNED16: return QStringLiteral("UNSIGNED16");
    case ECT_UNSIGNED24: return QStringLiteral("UNSIGNED24");
    case ECT_UNSIGNED32: return QStringLiteral("UNSIGNED32");
    case ECT_UNSIGNED64: return QStringLiteral("UNSIGNED64");
    case ECT_REAL32: return QStringLiteral("REAL32");
    case ECT_REAL64: return QStringLiteral("REAL64");
    case ECT_VISIBLE_STRING:
        return QStringLiteral("STRING(%1)").arg((bitLength + 7) / 8);
    case ECT_OCTET_STRING:
    default:
        return QStringLiteral("RAW");
    }
}

explorer::AccessMode EthercatExplorerController::accessModeFromCoe(
    quint16 objectAccess, int state)
{
    int stateBit = -1;
    switch (state & 0x0f) {
    case PreOp: stateBit = 0; break;
    case SafeOp: stateBit = 1; break;
    case Op: stateBit = 2; break;
    default: break;
    }
    if (stateBit < 0) {
        return explorer::AccessMode::None;
    }

    const bool readable = (objectAccess & (quint16(1) << stateBit)) != 0;
    const bool writable = (objectAccess & (quint16(1) << (stateBit + 3))) != 0;
    if (readable && writable) {
        return explorer::AccessMode::ReadWrite;
    }
    if (readable) {
        return explorer::AccessMode::ReadOnly;
    }
    if (writable) {
        return explorer::AccessMode::WriteOnly;
    }
    return explorer::AccessMode::None;
}

} // namespace Backend

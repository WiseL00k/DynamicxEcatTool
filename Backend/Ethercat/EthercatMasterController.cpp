#include "EthercatMasterController.h"

#include "Backend/Ethercat/EthercatSlaveLoader.h"
#include "SOEM_interface/EcatMasterBus.h"
#include <algorithm>
#include <vector>

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

void EthercatMasterController::stopExplorer()
{
    if (master_) {
        master_->resetExplorer();
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

soem_interface::BusScanResult EthercatMasterController::startExplorer(const std::string& nicName)
{
    soem_interface::BusScanResult result;
    if (master_) {
        result.errorCode = soem_interface::error::EcatInitFailed;
        result.error = "已有 EtherCAT 主站会话正在运行。";
        return result;
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName);
    result = master_->scanForSlaves();
    if (!result.success) {
        reset();
        return result;
    }

    connected_ = true;
    configuredSlaveCount_ = result.slaveCount;
    return result;
}

soem_interface::DcStartResult EthercatMasterController::startDcTest(
    const std::string& nicName,
    const soem_interface::DcTestOptions& options)
{
    std::shared_ptr<soem_interface::EcatMasterBus> master;
    bool stopRequested = false;
    {
        std::lock_guard lock(dcMutex_);
        if (master_) {
            return {false, false, "已有 EtherCAT 主站会话正在运行。"};
        }
        master = std::make_shared<soem_interface::EcatMasterBus>(nicName);
        master_ = master;
        connected_ = false;
        configuredSlaveCount_ = 0;
        lastDcSnapshot_ = {};
        stopRequested = dcStopRequested_;
    }
    if (stopRequested) {
        master->requestDcStop();
    }

    soem_interface::DcStartResult result = master->startDcTest(options);
    const soem_interface::DcTestSnapshot snapshot = master->dcTestSnapshot();
    {
        std::lock_guard lock(dcMutex_);
        lastDcSnapshot_ = snapshot;
        if (result.success) {
            connected_ = true;
            configuredSlaveCount_ = static_cast<int>(snapshot.slaves.size());
        } else if (master_ == master) {
            master_.reset();
            connected_ = false;
            configuredSlaveCount_ = 0;
        }
    }
    return result;
}

void EthercatMasterController::requestDcStop()
{
    std::shared_ptr<soem_interface::EcatMasterBus> master;
    {
        std::lock_guard lock(dcMutex_);
        dcStopRequested_ = true;
        master = master_;
    }
    if (master) {
        master->requestDcStop();
    }
}

void EthercatMasterController::stopDcTest()
{
    std::shared_ptr<soem_interface::EcatMasterBus> master;
    {
        std::lock_guard lock(dcMutex_);
        master = master_;
    }
    if (master) {
        master->stopDcTest();
    }

    const soem_interface::DcTestSnapshot snapshot = master
        ? master->dcTestSnapshot()
        : soem_interface::DcTestSnapshot{};
    {
        std::lock_guard lock(dcMutex_);
        if (master) {
            lastDcSnapshot_ = snapshot;
        }
        if (!master || master_ == master) {
            master_.reset();
            connected_ = false;
            configuredSlaveCount_ = 0;
            dcStopRequested_ = false;
        }
    }
}

soem_interface::DcTestSnapshot EthercatMasterController::dcTestSnapshot() const
{
    std::shared_ptr<soem_interface::EcatMasterBus> master;
    soem_interface::DcTestSnapshot retained;
    {
        std::lock_guard lock(dcMutex_);
        master = master_;
        retained = lastDcSnapshot_;
    }
    return master ? master->dcTestSnapshot() : retained;
}

void EthercatMasterController::resetDcStatistics()
{
    std::shared_ptr<soem_interface::EcatMasterBus> master;
    {
        std::lock_guard lock(dcMutex_);
        master = master_;
    }
    if (master) {
        master->resetDcStatistics();
    }
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

MasterStartResult validateMitDebugScan(const soem_interface::BusScanResult& scan)
{
    if (!scan.success) return {false, 0, scan.errorCode, {}, QString::fromStdString(scan.error)};
    if (scan.slaveCount != 1 || scan.slaves.size() != 1)
        return {false, scan.slaveCount, soem_interface::error::InvalidSlave, {},
                QStringLiteral("达妙电机调试仅允许一块从站，实际发现 %1 块；未进入 OP").arg(scan.slaveCount)};
    const auto& slave = scan.slaves.front();
    // Identity and packed 149-byte process images from the repository MIT ESI
    // and MitEcatSlave implementation. Do not treat the CAN gateway as MIT.
    if (!scan.mappingReady || !scan.activePdoComplete || slave.position != 1 || slave.vendorId != 0 ||
        slave.productCode != 0xAB124 || slave.revision != 2 ||
        slave.outputBits != 149 * 8 || slave.inputBits != 149 * 8)
        return {false, 1, soem_interface::error::InvalidSlave, {},
                QStringLiteral("从站身份或 PDO 长度与原 MIT 从站不匹配（需要 0/0xAB124/2、双向 149 字节）；未进入 OP")};
    for (auto direction : {soem_interface::PdoDirection::Rx, soem_interface::PdoDirection::Tx}) {
        std::vector<soem_interface::ActivePdoEntry> actual;
        for (const auto& entry : scan.activePdos)
            if (entry.slave == 1 && entry.direction == direction) actual.push_back(entry);
        std::sort(actual.begin(), actual.end(), [](const auto& a, const auto& b) { return a.bitOffset < b.bitOffset; });
        struct Entry { uint16_t index; uint8_t sub; uint16_t bits; uint32_t offset; };
        std::vector<Entry> expected;
        uint32_t offset = 0;
        const auto add = [&](uint16_t index, uint8_t sub, uint16_t bits) {
            expected.push_back({index, sub, bits, offset}); offset += bits;
        };
        const bool outputs = direction == soem_interface::PdoDirection::Rx;
        if (outputs) add(0x6040, 0, 32);
        for (int channel = 0; channel < 2; ++channel)
            for (uint8_t slot = 1; slot <= 9; ++slot) add(uint16_t((outputs ? 0x7000 : 0x6000) + channel), slot, 64);
        add(outputs ? 0x7002 : 0x600C, 0, 8);
        if (!outputs) add(0x6041, 0, 32);
        bool matches = actual.size() == expected.size();
        for (size_t i = 0; matches && i < expected.size(); ++i)
            matches = actual[i].index == expected[i].index && actual[i].subindex == expected[i].sub &&
                      actual[i].bitLength == expected[i].bits && actual[i].bitOffset == expected[i].offset;
        if (!matches)
            return {false, 1, soem_interface::error::InvalidSlave, {},
                    QStringLiteral("原 MIT 从站 %1PDO 字段布局不匹配九槽位协议；未进入 OP")
                        .arg(outputs ? QStringLiteral("Rx") : QStringLiteral("Tx"))};
    }
    return {true, 1};
}

MasterStartResult EthercatMasterController::enterMitDebugMode(const std::string& nicName)
{
    if (master_) {
        return {false, configuredSlaveCount_};
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName);

    const auto preflight = validateMitDebugScan(master_->scanForSlaves());
    if (!preflight.ok) {
        reset();
        return preflight;
    }

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

    connected_ = true;
    return {true, configuredSlaveCount_};
}

} // namespace Backend

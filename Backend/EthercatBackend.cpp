#include "EthercatBackend.h"
#include "DxSlaveConfigurationParser.h"
#include "EcatSlaveConfigurationParser.h"
#include "EcatSlave/RmEcatSlave.h"
#include "SOEM_interface/EcatMasterBus.h"
#include <bitset>
#include <qdebug.h>
#include <qurl.h>

namespace Backend {

static QString errorString(SoemInterfaceErrorCode code)
{
    switch(code)
    {
    case InvalidSlave: return "从站数量不一致,请检查配置文件!";
    case InvalidNicName: return "网卡名称错误";
    case NoSlaveFound: return "未找到从站";
    case EcatInitFailed: return "EtherCAT初始化失败，网卡选择错误或权限不足";
    case RequestOpFailed: return "请求OP状态失败";
    case RxPdoSizeMismatch: return "RxPDO大小不匹配";
    case TxPdoSizeMismatch: return "TxPDO大小不匹配";
    case EthercatNotOperational: return "EtherCAT未运行";
    case InvaidEEpromHexFile: return "无效HEX文件";
    default: return "未知错误";
    }
}

EthercatBackend::EthercatBackend(QObject* parent)
    : QObject(parent)
{}

EthercatBackend::~EthercatBackend() = default;

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

void EthercatBackend::refreshNics()
{
    nicList_.clear();

    adapters = soem_interface::SoemUtils::scanAdapters();
    for (const auto& a : adapters) {
        nicList_ << QString::fromStdString(a.desc);
    }

    emit nicListChanged();
}

void EthercatBackend::changedSelectedNic(const int& nicIndex)
{
    nicName_ = adapters.at(nicIndex).name;
    emit logUpdated(QString("选择了网卡: %1").arg(QString(nicName_.c_str())));;
}

void EthercatBackend::startTest()
{
    if (master_) {
        return;
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(
        nicName_
        );

    SoemInterfaceErrorCode errorCode = master_->startTest();
    bool ok = (errorCode == NoError);

    connected_ = ok;
    emit connectedChanged();

    if (ok) {
        emit slaveCountChanged();
        workerThread_ = new QThread(this);
        testWorker_ = new EthercatTestWorker(master_.get());

        testWorker_->moveToThread(workerThread_);

        connect(workerThread_, &QThread::started,
                testWorker_, &EthercatTestWorker::run);

        connect(testWorker_, &EthercatTestWorker::logUpdated,
                this, &EthercatBackend::logUpdated);

        connect(testWorker_, &EthercatTestWorker::finished,
                workerThread_, &QThread::quit);

        connect(workerThread_, &QThread::finished,
                workerThread_, &QObject::deleteLater);

        workerThread_->start();
    }
    else {
        master_.reset();
        emit logUpdated(QString("初始化失败，无法连接到网卡 %1").arg(QString::fromStdString(nicName_)));
        emit soemErrorOccurred(errorString(errorCode));
    }
    emit connectedUpdated(ok);
}

void EthercatBackend::stopTest()
{
    if (!master_ || !connected_)
        return;

    if (testWorker_) {
        testWorker_->stop();
        testWorker_ = nullptr;
    }
    else
    {
        return;
    }

    // 等待工作线程结束
    if (workerThread_ && workerThread_->isRunning()) {
        workerThread_->quit();
        workerThread_->wait(500); // 最多等 0.5 秒
    }

    master_->stop();
    master_.reset();

    connected_ = false;
    slaveCount_ = 0;

    emit connectedChanged();
    emit slaveCountChanged();
    emit connectedUpdated(0);
}

QString EthercatBackend::changeConfigFilePath(const QString &config_file_path){
    QUrl url(config_file_path);
    QString localPath = url.toLocalFile();
    qDebug() << localPath;
    configFilePath_ = localPath.toStdString();
    return localPath;
}


bool EthercatBackend::parseSlaveInfo(){
    if(!master_)
        return false;
    try {
        EcatSlaveConfigurationParser parser(configFilePath_);
        EcatDeviceConfiguration ecatDeviceConfiguration = parser.getConfiguration();
        std::cout << ecatDeviceConfiguration.slaveConfigurations_.size() << std::endl;
        slaveCount_ = ecatDeviceConfiguration.slaveConfigurations_.size();
        deviceModel_.clear();
        for(int address = 1; address <= slaveCount_; ++address){
            auto it = ecatDeviceConfiguration.slaveConfigurations_.at(address);
            DxSlave::DxSlaveConfigurationParser dxParser(it.configuration_file_path);
            DxSlave::DxSlaveConfiguration dxDeviceConfiguration = dxParser.getConfiguration();
            if(it.type == "Rm")
            {
                std::cout << "Slave " << address << " : " << it.name << " , Type: " << it.type << std::endl;
                std::cout << "  CAN0 Motors: " << dxDeviceConfiguration.can0MotorConfigurations_.size() << std::endl;
                std::cout << "  CAN1 Motors: " << dxDeviceConfiguration.can1MotorConfigurations_.size() << std::endl;
                std::shared_ptr<rm_ecat_slave::standard::RmEcatSlave> rmSlavePtr = std::make_shared<rm_ecat_slave::standard::RmEcatSlave>(
                    it.name,
                    master_.get(),
                    address
                    );
                rmSlavePtr->setConfiguration(dxDeviceConfiguration);
                master_->addSlave(rmSlavePtr);
            }
            else if(it.type == "Mit")
            {
                std::cout << "Slave " << address << " : " << it.name << " , Type: " << it.type << std::endl;
                std::cout << "  CAN0 Motors: " << dxDeviceConfiguration.can0MotorConfigurations_.size() << std::endl;
                std::cout << "  CAN1 Motors: " << dxDeviceConfiguration.can1MotorConfigurations_.size() << std::endl;
                std::shared_ptr<rm_ecat_slave::mit::MitEcatSlave> mitSlavePtr = std::make_shared<rm_ecat_slave::mit::MitEcatSlave>(
                    it.name,
                    master_.get(),
                    address
                    );
                mitSlavePtr->setConfiguration(dxDeviceConfiguration);
                master_->addSlave(mitSlavePtr);
            }

            deviceModel_.addSlaveHeader(
                QString("从站%1: %2 (%3 电机)")
                    .arg(address)
                    .arg(QString::fromStdString(it.name))
                    .arg(dxDeviceConfiguration.motorCount_)
                );

            for(auto& [id, motorConfig] : dxDeviceConfiguration.can0MotorConfigurations_)
            {
                deviceModel_.addMotor(
                    QString::fromStdString(motorConfig.name_),
                    0,
                    id
                    );
            }

            for(auto& [id, motorConfig] : dxDeviceConfiguration.can1MotorConfigurations_)
            {
                deviceModel_.addMotor(
                    QString::fromStdString(motorConfig.name_),
                    1,
                    id
                    );
            }

            if(dxDeviceConfiguration.can0ImuConfiguration_ != nullptr)
            {
                std::string imuName = dxDeviceConfiguration.can0ImuConfiguration_->name_;
                deviceModel_.addImu(
                    QString::fromStdString(imuName),
                    static_cast<int>(DxSlave::CanBus::CAN0)
                    );
            }
            if(dxDeviceConfiguration.can1ImuConfiguration_ != nullptr)
            {
                std::string imuName = dxDeviceConfiguration.can1ImuConfiguration_->name_;
                deviceModel_.addImu(
                    QString::fromStdString(imuName),
                    static_cast<int>(DxSlave::CanBus::CAN1)
                    );
            }

            emit motorStatusListChanged();
        }
        return true;
    } catch (...) {
        return false;
    }
}

void EthercatBackend::startCommunication(){
    if (master_) {
        return;
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName_);

    if(!parseSlaveInfo())
    {
        emit connectedUpdated(false);
        emit logUpdated(QString("yaml文件错误！请检查格式"));
        master_.reset();
        return;
    }

    SoemInterfaceErrorCode errorCode = master_->start();
    bool ok = (errorCode == NoError);
    connected_ = ok;
    emit connectedChanged();

    if(slaveCount_ != master_->slaveCount())
    {
        emit logUpdated(QString("初始化失败，从站数量不一致！\n实际从站数量:%1 配置从站数量:%2").arg(master_->slaveCount())
                        .arg(slaveCount_));
        errorCode = InvalidSlave;
        emit soemErrorOccurred(errorString(errorCode));
        master_.reset();
    }
    else if (ok) {
        emit slaveCountChanged();
        workerThread_ = new QThread(this);
        worker_ = new EthercatWorker(master_.get());

        worker_->moveToThread(workerThread_);

        connect(worker_, &EthercatWorker::setDeviceOnlineStatus,
                this, &EthercatBackend::setDeviceOnlineStatus);

        connect(workerThread_, &QThread::started,
                worker_, &EthercatWorker::run);

        connect(worker_, &EthercatWorker::logUpdated,
                this, &EthercatBackend::logUpdated);

        connect(worker_, &EthercatWorker::finished,
                workerThread_, &QThread::quit);

        connect(worker_, &EthercatWorker::finished,
                this, &EthercatBackend::clearMotorStatusList);

        connect(workerThread_, &QThread::finished,
                workerThread_, &QObject::deleteLater);

        workerThread_->start();
    }
    else {
        master_.reset();
        emit logUpdated(QString("初始化失败，无法连接到网卡 %1").arg(QString::fromStdString(nicName_)));
        emit soemErrorOccurred(errorString(errorCode));
    }
    emit connectedUpdated(ok);
}

void EthercatBackend::stopCommunication()
{
    if (!master_ || !connected_)
        return;

    if (worker_) {
        worker_->stop();
        worker_ = nullptr;
    }
    else
    {
        return;
    }
    // 等待工作线程结束
    if (workerThread_ && workerThread_->isRunning()) {
        workerThread_->quit();
        workerThread_->wait(500); // 最多等 0.5 秒
    }

    master_->stop();
    master_.reset();

    connected_ = false;
    slaveCount_ = 0;

    emit connectedChanged();
    emit slaveCountChanged();
    emit connectedUpdated(0);
}

void EthercatBackend::clearMotorStatusList()
{
    deviceModel_.clearAllOnline();

    emit logUpdated("");
}

void EthercatTestWorker::testLogRefresh()
{
    static int cycle = 0;
    cycle %= 1000000;
    QString line;
    ecx_contextt& ctx = master_->getContext();
    ec_groupt *group = &ctx.grouplist[0];
    line = QString::asprintf(
        "Processdata cycle %5d , Wck %3d, DCtime %12lld, O:",
        cycle++,
        master_->getWKC(),
        static_cast<long long>(ctx.DCtime)
        );
    int size = group->Obytes < 8 ? group->Obytes : 8;
    for (int j = 0; j < size; j++)
    {
        line += QString(" %1").arg(*(ctx.slavelist[0].outputs + j), 2, 16, QChar('0'));
    }

    line += " I:";
    for (int j = 0; j < size; j++)
    {
        line += QString(" %1").arg(*(ctx.slavelist[0].inputs + j), 2, 16, QChar('0'));
    }

    emit logUpdated(line);
}

void EthercatWorker::pdoLogRefresh()
{
    QString line;
    for(int address = 1; address <= master_->slaveCount(); address++)
    {
        auto slave = master_->getSlave(address);
        if(slave->getType() == "Rm")
        {
            rm_ecat_slave::standard::RmEcatSlave* rmSlave = dynamic_cast<rm_ecat_slave::standard::RmEcatSlave*>(slave.get());
            rm_ecat_slave::standard::Reading reading;
            rmSlave->getReading(reading);
            uint32_t raw = reading.getStatusword().getRaw();
            std::bitset<32> bits(raw);
            line += QString("\nSlave %1 (%2) \nStatusword: %3")
                        .arg(address)
                        .arg(QString::fromStdString(slave->getName()))
                        .arg(QString::fromStdString(bits.to_string()));
            for(auto& [id, motorConfig] : rmSlave->getConfiguration().can0MotorConfigurations_)
            {
                emit setDeviceOnlineStatus(QString::fromStdString(motorConfig.name_), reading.getStatusword().isOnline(DxSlave::CanBus::CAN0, id));
            }
            for(auto& [id, motorConfig] : rmSlave->getConfiguration().can1MotorConfigurations_)
            {
                emit setDeviceOnlineStatus(QString::fromStdString(motorConfig.name_), reading.getStatusword().isOnline(DxSlave::CanBus::CAN1, id));
            }
            if(rmSlave->getConfiguration().can0ImuConfiguration_ != nullptr)
            {
                std::string imuName = rmSlave->getConfiguration().can0ImuConfiguration_->name_;
                emit setDeviceOnlineStatus(QString::fromStdString(imuName), reading.getStatusword().isImuOnline(DxSlave::CanBus::CAN0));
            }
            if(rmSlave->getConfiguration().can1ImuConfiguration_ != nullptr)
            {
                std::string imuName = rmSlave->getConfiguration().can1ImuConfiguration_->name_;
                emit setDeviceOnlineStatus(QString::fromStdString(imuName), reading.getStatusword().isImuOnline(DxSlave::CanBus::CAN1));
            }
        }
        else if(slave->getType() == "Mit")
        {
            rm_ecat_slave::mit::MitEcatSlave* mitSlave = dynamic_cast<rm_ecat_slave::mit::MitEcatSlave*>(slave.get());
            rm_ecat_slave::mit::Reading reading;
            mitSlave->getReading(reading);
            uint32_t raw = reading.getStatusword().getRaw();
            std::bitset<32> bits(raw);
            line += QString("\nSlave %1 (%2) \nStatusword: %3")
                        .arg(address)
                        .arg(QString::fromStdString(slave->getName()))
                        .arg(QString::fromStdString(bits.to_string()));
            for(auto& [id, motorConfig] : mitSlave->getConfiguration().can0MotorConfigurations_)
            {
                emit setDeviceOnlineStatus(QString::fromStdString(motorConfig.name_), reading.getStatusword().isOnline(DxSlave::CanBus::CAN0, id));
            }
            for(auto& [id, motorConfig] : mitSlave->getConfiguration().can1MotorConfigurations_)
            {
                emit setDeviceOnlineStatus(QString::fromStdString(motorConfig.name_), reading.getStatusword().isOnline(DxSlave::CanBus::CAN1, id));
            }
        }
    }

    emit logUpdated(line);
}

void EthercatBackend::enterPreOpAll()
{
    if (master_) {
        return;
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName_);

    SoemInterfaceErrorCode errorCode = master_->initMaster();
    bool ok = (errorCode == NoError);
    connected_ = ok;
    emit connectedChanged();
    emit connectedUpdated(ok);
    if(ok)
    {
        master_->requestPreOp();
    }
    else
    {
        master_.reset();
        emit soemErrorOccurred(errorString(errorCode));
    }
}

void EthercatBackend::exitPreOpAll()
{
    if (!master_ || !connected_)
        return;

    if(worker_ || testWorker_)
        return;

    master_->closeMaster();
    master_.reset();

    connected_ = false;

    emit connectedChanged();
    emit connectedUpdated(0);
}

void EthercatBackend::enterMitSlaveDebugMode()
{
    if (master_) {
        return;
    }

    master_ = std::make_shared<soem_interface::EcatMasterBus>(nicName_);

    std::shared_ptr<rm_ecat_slave::mit::MitEcatSlave> mitSlavePtr = std::make_shared<rm_ecat_slave::mit::MitEcatSlave>(
        "mit",
        master_.get(),
        1
        );
    master_->addSlave(mitSlavePtr);
    slaveCount_ = 1;

    SoemInterfaceErrorCode errorCode = master_->start();
    bool ok = (errorCode == NoError);
    connected_ = ok;
    emit connectedChanged();

    if(slaveCount_ != master_->slaveCount())
    {
        emit logUpdated(QString("初始化失败，从站数量不一致！\n实际从站数量:%1 配置从站数量:%2").arg(master_->slaveCount())
                            .arg(slaveCount_));
        errorCode = InvalidSlave;
        emit soemErrorOccurred(errorString(errorCode));
        master_.reset();
    }
    else if (ok) {
        emit slaveCountChanged();
        workerThread_ = new QThread(this);
        worker_ = new EthercatWorker(master_.get());

        worker_->moveToThread(workerThread_);

        connect(worker_, &EthercatWorker::setDeviceOnlineStatus,
                this, &EthercatBackend::setDeviceOnlineStatus);

        connect(workerThread_, &QThread::started,
                worker_, &EthercatWorker::run);

        connect(worker_, &EthercatWorker::logUpdated,
                this, &EthercatBackend::logUpdated);

        connect(worker_, &EthercatWorker::finished,
                workerThread_, &QThread::quit);

        connect(worker_, &EthercatWorker::finished,
                this, &EthercatBackend::clearMotorStatusList);

        connect(workerThread_, &QThread::finished,
                workerThread_, &QObject::deleteLater);

        workerThread_->start();
    }
    else {
        master_.reset();
        emit logUpdated(QString("初始化失败，无法连接到网卡 %1").arg(QString::fromStdString(nicName_)));
        emit soemErrorOccurred(errorString(errorCode));
    }
    emit connectedUpdated(ok);
}

void EthercatBackend::exitMitSlaveDebugMode()
{
    if (!master_ || !connected_)
        return;

    if (worker_) {
        worker_->stop();
        worker_ = nullptr;
    }
    else
    {
        return;
    }
    // 等待工作线程结束
    if (workerThread_ && workerThread_->isRunning()) {
        workerThread_->quit();
        workerThread_->wait(500); // 最多等 0.5 秒
    }

    master_->stop();
    master_.reset();

    connected_ = false;
    slaveCount_ = 0;

    emit connectedChanged();
    emit slaveCountChanged();
    emit connectedUpdated(0);
}

void EthercatBackend::enableMitSlaveMotors()
{
    auto slavePtr = master_->getSlave(1);
    rm_ecat_slave::mit::MitEcatSlave* mitSlave = dynamic_cast<rm_ecat_slave::mit::MitEcatSlave*>(slavePtr.get());
    mitSlave->getCommandHandle().enableMotors();
}

void EthercatBackend::disableMitSlaveMotors()
{
    auto slavePtr = master_->getSlave(1);
    rm_ecat_slave::mit::MitEcatSlave* mitSlave = dynamic_cast<rm_ecat_slave::mit::MitEcatSlave*>(slavePtr.get());
    mitSlave->getCommandHandle().disableMotors();
}

bool EthercatBackend::applySDOConfigsQml(const QVariantList& list)
{
    if (!master_ || !connected_)
        return false;
    auto configs = parseSDOConfigs(list);

    bool ret = master_->applySDOConfigs(configs);
    if(ret)
    {
        emit soemErrorOccurred("SDO配置成功!");
    }
    else
    {
        emit soemErrorOccurred("SDO配置失败!请检查参数或从站状态");
    }
    return ret;
}

std::vector<soem_interface::SDOConfig> EthercatBackend::parseSDOConfigs(const QVariantList& list)
{
    std::vector<soem_interface::SDOConfig> result;
    result.reserve(list.size());

    for (const auto& item : list)
    {
        QVariantMap m = item.toMap();

        soem_interface::SDOConfig cfg;
        cfg.slave    = static_cast<uint16_t>(m["slave"].toUInt());
        cfg.index    = static_cast<uint16_t>(m["index"].toUInt());
        cfg.subindex = static_cast<uint8_t>(m["subindex"].toUInt());

        // ---------- type ----------
        QString typeStr = m["type"].toString().toLower();
        if (typeStr == "write")
            cfg.type = soem_interface::SDOType::WRITE;
        else
            cfg.type = soem_interface::SDOType::READ;

        // ---------- data ----------
        if (cfg.type == soem_interface::SDOType::WRITE)
        {
            QVariantList dataList = m["data"].toList();
            cfg.data.reserve(dataList.size());

            for (const auto& v : dataList)
                cfg.data.push_back(static_cast<uint8_t>(v.toUInt()));
        }
        else
        {
            cfg.expected_size = m["expected_size"].toInt();
        }

        result.push_back(std::move(cfg));
    }

    return result;
}

void EthercatBackend::refreshNicsAsync()
{
    QtConcurrent::run([this]() {
        this->refreshNics();  // 原来的耗时函数
    });
}

void EthercatBackend::flashEEprom(int slaveId, const QString &filePath)
{
    if(filePath.isEmpty())
    {
        emit logUpdated("请选择HEX文件!");
        return;
    }
    QUrl url(filePath);
    QString localPath = url.toLocalFile();
    std::string hexFilePath_ = localPath.toStdString();

    auto progress = std::make_shared<std::atomic<int>>(0);
    auto finished = std::make_shared<std::atomic<bool>>(false);

    // 计时器
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    // 伪进度线程
    QtConcurrent::run([=]() {
        while (!(*finished)) {
            int p = progress->load();
            if (p < 98) {
                int delta = std::max(1, int((98 - p) * (0.03 + (rand() % 5) * 0.01)));
                p += delta;
                progress->store(p);
                emit flashProgress(p);
            }
            QThread::msleep(100);
        }
    });

    // 真正烧录线程
    QtConcurrent::run([=]() {
        soem_interface::EEpromTool eepromTool(
            nicName_, slaveId, MODE_WRITEINTEL, hexFilePath_.c_str());

        emit logUpdated("开始烧录 EEPROM...");
        SoemInterfaceErrorCode errorCode =
            eepromTool.work(nicName_, slaveId, MODE_WRITEINTEL, hexFilePath_.c_str());

        *finished = true;

        if (errorCode != soem_interface::error::NoError) {
            emit soemErrorOccurred(errorString(errorCode));
            return;
        }

        // 计算耗时（毫秒）
        qint64 elapsed_ms = timer->elapsed();

        progress->store(100);
        emit flashProgress(100);

        emit logUpdated(QString("烧录完成，用时 %1 ms").arg(elapsed_ms));
        emit flashFinished(true, QString("OK (%1 ms)").arg(elapsed_ms));
    });
}

void EthercatBackend::sendMitFrameQml(int canBus, int canId, const QVariantList &data)
{
    if (data.size() != 8)
    {
        qDebug() << "MIT帧长度错误";
        return;
    }

    uint8_t frame[8]{};

    for (int i = 0; i < 8; ++i)
    {
        frame[i] = static_cast<uint8_t>(data[i].toUInt());
    }

    uint64_t mit_frame{};
    memcpy(&mit_frame, frame, 8);
    auto slavePtr = master_->getSlave(1);
    rm_ecat_slave::mit::MitEcatSlave* mitSlave = dynamic_cast<rm_ecat_slave::mit::MitEcatSlave*>(slavePtr.get());
    mitSlave->getCommandHandle().setCanCommand(static_cast<DxSlave::CanBus>(canBus), canId, mit_frame);
}

void EthercatBackend::clearMitFrameQml(int canBus, int canId)
{
    uint64_t mit_frame = 0xFF070000F07FFF7FULL;
    auto slavePtr = master_->getSlave(1);
    rm_ecat_slave::mit::MitEcatSlave* mitSlave = dynamic_cast<rm_ecat_slave::mit::MitEcatSlave*>(slavePtr.get());
    mitSlave->getCommandHandle().setCanCommand(static_cast<DxSlave::CanBus>(canBus), canId, mit_frame);
}

}

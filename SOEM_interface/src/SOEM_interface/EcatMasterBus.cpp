#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/EcatSlaveBase.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace soem_interface {

EcatMasterBus::EcatMasterBus(const std::string& ifname)
    : nic_name_(ifname)
{}

EcatMasterBus::~EcatMasterBus()
{
    stop();
}

void EcatMasterBus::setNICName(const std::string& ifname)
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    nic_name_ = ifname;
}

SoemInterfaceErrorCode EcatMasterBus::startTest()
{
    {
        std::lock_guard<std::recursive_mutex> lock(contextMutex_);
        if (running_) {
            return NoError;
        }

        if (nic_name_.empty()) {
            return InvalidNicName;
        }

        const SoemInterfaceErrorCode errorCode = initMaster();
        if (errorCode != NoError) {
            return errorCode;
        }

        requestOperational();

        if (!operational_) {
            closeMaster();
            return RequestOpFailed;
        }

        running_ = true;
    }

    cyclicThread_ = std::thread(&EcatMasterBus::cyclicTestTask, this);
    checkThread_  = std::thread(&EcatMasterBus::checkTask, this);

    return NoError;
}

SoemInterfaceErrorCode EcatMasterBus::start()
{
    {
        std::lock_guard<std::recursive_mutex> lock(contextMutex_);
        if (running_) {
            return NoError;
        }

        if (nic_name_.empty()) {
            return InvalidNicName;
        }

        const SoemInterfaceErrorCode errorCode = initMaster();
        if (errorCode != NoError) {
            return errorCode;
        }

        for (auto& slave : slaves_) {
            if (!slave->startup()) {
                closeMaster();
                return InvalidSlave;
            }

            const auto pdoInfo = slave->getCurrentPdoInfo();
            const auto address = static_cast<int>(slave->getAddress());
            if (address <= 0 || address > context_.slavecount) {
                closeMaster();
                return InvalidSlave;
            }

            if (pdoInfo.rxPdoSize_ != context_.slavelist[address].Obytes) {
                closeMaster();
                return RxPdoSizeMismatch;
            }
            if (pdoInfo.txPdoSize_ != context_.slavelist[address].Ibytes) {
                closeMaster();
                return TxPdoSizeMismatch;
            }
        }

        requestOperational();

        if (!operational_) {
            closeMaster();
            return RequestOpFailed;
        }

        running_ = true;
    }

    cyclicThread_ = std::thread(&EcatMasterBus::cyclicTask, this);
    checkThread_  = std::thread(&EcatMasterBus::checkTask, this);

    return NoError;
}

void EcatMasterBus::stop()
{
    if (running_) {
        running_ = false;

        if (cyclicThread_.joinable()) {
            cyclicThread_.join();
        }

        if (checkThread_.joinable()) {
            checkThread_.join();
        }
    }

    if (master_init_) {
        closeMaster();
    }

    operational_ = false;
}

SoemInterfaceErrorCode EcatMasterBus::initMaster()
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);

    if (master_init_) {
        return NoError;
    }

    if (ecx_init(&context_, nic_name_.c_str()) <= 0) {
        std::cerr << "ecx_init failed\n";
        return EcatInitFailed;
    }

    if (ecx_config_init(&context_) <= 0) {
        std::cerr << "No slaves found\n";
        ecx_close(&context_);
        return NoSlaveFound;
    }

    ecx_statecheck(&context_, 0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE);

    ec_groupt* group = &context_.grouplist[0];
    ecx_config_map_group(&context_, IOmap_, 0);
    expectedWKC = (group->outputsWKC * 2) + group->inputsWKC;

    for (int slave = 1; slave <= context_.slavecount; slave++) {
        if (context_.slavelist[slave].inputs != nullptr) {
            std::memset(context_.slavelist[slave].inputs, 0, context_.slavelist[slave].Ibytes);
        }
        if (context_.slavelist[slave].outputs != nullptr) {
            std::memset(context_.slavelist[slave].outputs, 0, context_.slavelist[slave].Obytes);
        }
    }

    ecx_configdc(&context_);
    ecx_statecheck(&context_, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

    master_init_ = true;
    return NoError;
}

SoemInterfaceErrorCode EcatMasterBus::closeMaster()
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    if (!master_init_) {
        return NoError;
    }

    context_.slavelist[0].state = EC_STATE_SAFE_OP;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

    context_.slavelist[0].state = EC_STATE_INIT;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_INIT, EC_TIMEOUTSTATE);

    ecx_close(&context_);
    master_init_ = false;
    operational_ = false;
    pre_op_ = false;
    init_ = false;
    wkc = 0;
    return NoError;
}

void EcatMasterBus::requestInit()
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    context_.slavelist[0].state = EC_STATE_INIT;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_INIT, EC_TIMEOUTSTATE);

    init_ = (context_.slavelist[0].state == EC_STATE_INIT);
}

void EcatMasterBus::requestPreOp()
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    context_.slavelist[0].state = EC_STATE_PRE_OP;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE);

    pre_op_ = (context_.slavelist[0].state == EC_STATE_PRE_OP);
}

void EcatMasterBus::requestOperational()
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    context_.slavelist[0].state = EC_STATE_OPERATIONAL;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);

    operational_ = (context_.slavelist[0].state == EC_STATE_OPERATIONAL);
}

bool EcatMasterBus::isOperational() const
{
    return operational_;
}

bool EcatMasterBus::isMasterInitialized() const
{
    return master_init_;
}

int EcatMasterBus::slaveCount() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    return context_.slavecount;
}

EcatSlaveBasePtr EcatMasterBus::findSlave(uint16_t address) const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    const auto it = std::find_if(slaves_.begin(), slaves_.end(), [address](const EcatSlaveBasePtr& slave) {
        return slave && slave->getAddress() == address;
    });
    return it == slaves_.end() ? nullptr : *it;
}

EcatSlaveBasePtr EcatMasterBus::getSlave(uint16_t address) const
{
    return findSlave(address);
}

std::vector<EcatSlaveBasePtr> EcatMasterBus::registeredSlaves() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    return slaves_;
}

ProcessDataSnapshot EcatMasterBus::processDataSnapshot() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);

    ProcessDataSnapshot snapshot;
    snapshot.workingCounter = wkc.load();
    snapshot.dcTime = static_cast<int64_t>(context_.DCtime);

    const ec_groupt* group = &context_.grouplist[0];
    const int previewSize = group->Obytes < 8 ? group->Obytes : 8;
    if (previewSize > 0 && context_.slavelist[0].outputs != nullptr) {
        snapshot.outputPreview.assign(
            context_.slavelist[0].outputs,
            context_.slavelist[0].outputs + previewSize);
    }
    if (previewSize > 0 && context_.slavelist[0].inputs != nullptr) {
        snapshot.inputPreview.assign(
            context_.slavelist[0].inputs,
            context_.slavelist[0].inputs + previewSize);
    }

    return snapshot;
}

void EcatMasterBus::cyclicTestTask()
{
    while (running_) {
        {
            std::lock_guard<std::recursive_mutex> lock(contextMutex_);
            ecx_send_processdata(&context_);
            wkc = ecx_receive_processdata(&context_, EC_TIMEOUTRET);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void EcatMasterBus::cyclicTask()
{
    while (running_) {
        {
            std::lock_guard<std::recursive_mutex> lock(contextMutex_);
            for (auto& slave : slaves_) {
                slave->updateWrite();
            }
            ecx_send_processdata(&context_);
            wkc = ecx_receive_processdata(&context_, EC_TIMEOUTRET);
            for (auto& slave : slaves_) {
                slave->updateRead();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void EcatMasterBus::checkTask()
{
    while (running_) {
        {
            std::lock_guard<std::recursive_mutex> lock(contextMutex_);
            if (context_.grouplist[0].docheckstate) {
                context_.grouplist[0].docheckstate = FALSE;
                ecx_readstate(&context_);

                for (int i = 1; i <= context_.slavecount; ++i) {
                    if (context_.slavelist[i].state != EC_STATE_OPERATIONAL) {
                        std::cerr << "Slave " << i << " not operational\n";
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool EcatMasterBus::isValidSlaveAddress(uint16_t slave) const
{
    return slave > 0 && static_cast<int>(slave) <= context_.slavecount;
}

void EcatMasterBus::readTxPdo(const uint16_t slave, int size, void* buf) const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    assert(isValidSlaveAddress(slave));
    if (!isValidSlaveAddress(slave)) {
        return;
    }

    assert(size == static_cast<int>(context_.slavelist[slave].Ibytes));
    if (size != static_cast<int>(context_.slavelist[slave].Ibytes) || context_.slavelist[slave].inputs == nullptr) {
        return;
    }

    std::memcpy(buf, context_.slavelist[slave].inputs, size);
}

void EcatMasterBus::writeRxPdo(const uint16_t slave, int size, const void* buf)
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    assert(isValidSlaveAddress(slave));
    if (!isValidSlaveAddress(slave)) {
        return;
    }

    assert(size == static_cast<int>(context_.slavelist[slave].Obytes));
    if (size != static_cast<int>(context_.slavelist[slave].Obytes) || context_.slavelist[slave].outputs == nullptr) {
        return;
    }

    std::memcpy(context_.slavelist[slave].outputs, buf, size);
}

bool EcatMasterBus::addSlave(const EcatSlaveBasePtr& slave)
{
    if (!slave) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    for (const auto& existingSlave : slaves_) {
        if (existingSlave && slave->getAddress() == existingSlave->getAddress()) {
            std::cerr << "Slave with address " << slave->getAddress() << " already exists\n";
            return false;
        }
    }

    slave->setEthercatBusBasePointer(this);
    slaves_.push_back(slave);
    std::sort(slaves_.begin(), slaves_.end(), [](const EcatSlaveBasePtr& a, const EcatSlaveBasePtr& b) {
        return a->getAddress() < b->getAddress();
    });
    return true;
}

std::string EcatMasterBus::getErrorString(ec_errort error)
{
    std::stringstream stream;
    stream << "Time: " << (static_cast<double>(error.Time.tv_sec) + (static_cast<double>(error.Time.tv_nsec) / 1000000000.0));

    switch (error.Etype) {
    case EC_ERR_TYPE_SDO_ERROR:
        stream << " SDO slave: " << error.Slave << " index: 0x" << std::setfill('0') << std::setw(4) << std::hex << error.Index << "."
               << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(error.SubIdx) << " error: 0x" << std::setfill('0')
               << std::setw(8) << std::hex << static_cast<unsigned>(error.AbortCode) << " " << ec_sdoerror2string(error.AbortCode);
        break;
    case EC_ERR_TYPE_EMERGENCY:
        stream << " EMERGENCY slave: " << error.Slave << " error: 0x" << std::setfill('0') << std::setw(4) << std::hex << error.ErrorCode;
        break;
    case EC_ERR_TYPE_PACKET_ERROR:
        stream << " PACKET slave: " << error.Slave << " index: 0x" << std::setfill('0') << std::setw(4) << std::hex << error.Index << "."
               << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(error.SubIdx) << " error: 0x" << std::setfill('0')
               << std::setw(8) << std::hex << error.ErrorCode;
        break;
    case EC_ERR_TYPE_SDOINFO_ERROR:
        stream << " SDO slave: " << error.Slave << " index: 0x" << std::setfill('0') << std::setw(4) << std::hex << error.Index << "."
               << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(error.SubIdx) << " error: 0x" << std::setfill('0')
               << std::setw(8) << std::hex << static_cast<unsigned>(error.AbortCode) << " " << ec_sdoerror2string(error.AbortCode);
        break;
    case EC_ERR_TYPE_SOE_ERROR:
        stream << " SoE slave: " << error.Slave << " index: 0x" << std::setfill('0') << std::setw(4) << std::hex << error.Index
               << " error: 0x" << std::setfill('0') << std::setw(8) << std::hex << static_cast<unsigned>(error.AbortCode) << " "
               << ec_soeerror2string(error.ErrorCode);
        break;
    case EC_ERR_TYPE_MBX_ERROR:
        stream << " MBX slave: " << error.Slave << " error: 0x" << std::setfill('0') << std::setw(8) << std::hex << error.ErrorCode << " "
               << ec_mbxerror2string(error.ErrorCode);
        break;
    default:
        stream << " MBX slave: " << error.Slave << " error: 0x" << std::setfill('0') << std::setw(8) << std::hex
               << static_cast<unsigned>(error.AbortCode);
        break;
    }
    return stream.str();
}

bool EcatMasterBus::checkForSdoErrors(const uint16_t slave, const uint16_t index)
{
    while (ecx_iserror(&context_)) {
        ec_errort error;
        if (ecx_poperror(&context_, &error)) {
            const std::string errorStr = getErrorString(error);
            std::cout << errorStr;
            if (error.Slave == slave && error.Index == index) {
                return true;
            }
        }
    }
    return false;
}

bool EcatMasterBus::sdoWrite(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf)
{
    std::lock_guard<std::recursive_mutex> guard(contextMutex_);
    assert(isValidSlaveAddress(slave));
    if (!isValidSlaveAddress(slave)) {
        return false;
    }

    const int writeWkc = ecx_SDOwrite(&context_, slave, index, subindex, static_cast<boolean>(completeAccess), size, buf, EC_TIMEOUTRXM);
    if (writeWkc <= 0) {
        std::cout << "Slave " << slave << ": Working counter too low (" << writeWkc << ") for writing SDO (ID: 0x" << std::setfill('0')
                  << std::setw(4) << std::hex << index << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                  << static_cast<uint16_t>(subindex) << ").";
        checkForSdoErrors(slave, index);
        std::cout << "[soem_interface::" << name_ << "] Slave: " << (findSlave(slave) ? findSlave(slave)->getName() : std::string("unknown"))
                  << " alStatusCode: 0x" << std::setfill('0') << std::setw(8) << std::hex
                  << context_.slavelist[slave].ALstatuscode << " " << ec_ALstatuscode2string(context_.slavelist[slave].ALstatuscode);
        return false;
    }
    return true;
}

bool EcatMasterBus::sdoRead(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf)
{
    std::lock_guard<std::recursive_mutex> guard(contextMutex_);
    assert(isValidSlaveAddress(slave));
    if (!isValidSlaveAddress(slave)) {
        return false;
    }

    const int requestedSize = size;
    const int readWkc = ecx_SDOread(&context_, slave, index, subindex, static_cast<boolean>(completeAccess), &size, buf, EC_TIMEOUTRXM);
    if (readWkc <= 0) {
        std::cout << "Slave " << slave << ": Working counter too low (" << readWkc << ") for reading SDO (ID: 0x" << std::setfill('0')
                  << std::setw(4) << std::hex << index << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                  << static_cast<uint16_t>(subindex) << ").";

        checkForSdoErrors(slave, index);
        std::cout << "[soem_interface::" << name_ << "] Slave: " << (findSlave(slave) ? findSlave(slave)->getName() : std::string("unknown"))
                  << " alStatusCode: 0x" << std::setfill('0') << std::setw(8) << std::hex
                  << context_.slavelist[slave].ALstatuscode << " " << ec_ALstatuscode2string(context_.slavelist[slave].ALstatuscode);
        return false;
    }
    if (size != requestedSize) {
        std::cout << "Slave " << slave << ": Size mismatch (expected " << requestedSize << " bytes, read " << size
                  << " bytes) for reading SDO (ID: 0x" << std::setfill('0') << std::setw(4) << std::hex << index
                  << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(subindex) << ").";
    }
    return true;
}

bool EcatMasterBus::applySDOConfigs(const std::vector<SDOConfig>& configs)
{
    std::lock_guard<std::recursive_mutex> guard(contextMutex_);
    if (!master_init_) {
        return false;
    }

    for (const auto& cfg : configs) {
        if (!isValidSlaveAddress(cfg.slave)) {
            return false;
        }

        int ret = 0;
        if (cfg.type == SDOType::WRITE) {
            ret = ecx_SDOwrite(&context_, cfg.slave, cfg.index, cfg.subindex, FALSE,
                               static_cast<int>(cfg.data.size()), const_cast<uint8_t*>(cfg.data.data()), EC_TIMEOUTRXM);
            checkForSdoErrors(cfg.slave, cfg.index);
            std::printf("[SDO WRITE] slave=%d index=0x%X sub=0x%X %s\n",
                        cfg.slave, cfg.index, cfg.subindex, ret > 0 ? "OK" : "FAIL");
        } else if (cfg.type == SDOType::READ) {
            std::vector<uint8_t> buffer(static_cast<size_t>(cfg.expected_size));
            int size = cfg.expected_size;

            ret = ecx_SDOread(&context_, cfg.slave, cfg.index, cfg.subindex, FALSE, &size, buffer.data(), EC_TIMEOUTRXM);
            checkForSdoErrors(cfg.slave, cfg.index);
            if (ret > 0) {
                std::printf("[SDO READ] slave=%d index=0x%X value=", cfg.slave, cfg.index);
                for (int i = 0; i < size; i++) {
                    std::printf("%02X ", buffer[static_cast<size_t>(i)]);
                }
                std::printf("\n");
            } else {
                std::printf("[SDO READ] FAIL slave=%d index=0x%X\n", cfg.slave, cfg.index);
            }
        }

        if (ret <= 0) {
            return false;
        }
    }

    return true;
}

} // namespace soem_interface

#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/EcatSlaveBase.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <chrono>
#include <mutex>
#include <iomanip>
#include <sstream>

namespace soem_interface {

namespace {
// constexpr int EC_TIMEOUTMON = 500;
}

EcatMasterBus::EcatMasterBus(const std::string& ifname)
    : nic_name_(ifname)
{

}

EcatMasterBus::~EcatMasterBus()
{
    stop();
}

void EcatMasterBus::setNICName(const std::string& ifname)
{
    nic_name_ = ifname;
}

SoemInterfaceErrorCode EcatMasterBus::startTest()
{
    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        if (running_)
            return NoError;;

        if (nic_name_.empty())
            return InvalidNicName;

        SoemInterfaceErrorCode errorCode = initMaster();
        if (errorCode != NoError)
            return errorCode;

        requestOperational();

        if (!operational_)
        {
            ecx_close(&context_);
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
        std::lock_guard<std::mutex> lock(contextMutex_);
        if (running_)
            return NoError;

        if (nic_name_.empty())
            return InvalidNicName;

        SoemInterfaceErrorCode errorCode = initMaster();
        if (errorCode != NoError)
            return errorCode;

        for(auto slave : slaves_)
        {
            slave->startup();
            auto pdoInfo = slave->getCurrentPdoInfo();
            if(pdoInfo.rxPdoSize_ != context_.slavelist[slave->getAddress()].Obytes)
                return RxPdoSizeMismatch;
            if(pdoInfo.txPdoSize_ != context_.slavelist[slave->getAddress()].Ibytes)
                return TxPdoSizeMismatch;
        }

        requestOperational();

        if (!operational_)
        {
            ecx_close(&context_);
            return RequestOpFailed;
        }
    }
    running_ = true;

    cyclicThread_ = std::thread(&EcatMasterBus::cyclicTask, this);
    checkThread_  = std::thread(&EcatMasterBus::checkTask, this);

    return NoError;
}

void EcatMasterBus::stop()
{
    if (!running_)
        return;

    running_ = false;

    if (cyclicThread_.joinable())
        cyclicThread_.join();

    if (checkThread_.joinable())
        checkThread_.join();

    closeMaster();
    operational_ = false;
}

SoemInterfaceErrorCode EcatMasterBus::initMaster()
{
    if (ecx_init(&context_, nic_name_.c_str()) <= 0)
    {
        std::cerr << "ecx_init failed\n";
        return EcatInitFailed;
    }

    if (ecx_config_init(&context_) <= 0)
    {
        std::cerr << "No slaves found\n";
        ecx_close(&context_);
        return NoSlaveFound;
    }

    ecx_statecheck(&context_, 0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE);

    ec_groupt *group = &context_.grouplist[0];
    ecx_config_map_group(&context_, IOmap_, 0);
    expectedWKC = (group->outputsWKC * 2) + group->inputsWKC;
    // Initialize the memory with zeroes.
    for (int slave = 1; slave <= context_.slavecount; slave++) {
        memset(context_.slavelist[slave].inputs, 0, context_.slavelist[slave].Ibytes);
        memset(context_.slavelist[slave].outputs, 0, context_.slavelist[slave].Obytes);
    }
    ecx_configdc(&context_);

    ecx_statecheck(&context_, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

    master_init_ = true;
    return NoError;
}

SoemInterfaceErrorCode EcatMasterBus::closeMaster()
{
    std::lock_guard<std::mutex> lock(contextMutex_);
    context_.slavelist[0].state = EC_STATE_SAFE_OP;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

    context_.slavelist[0].state = EC_STATE_INIT;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_INIT, EC_TIMEOUTSTATE);

    ecx_close(&context_);
    master_init_ = false;
    return NoError;
}

void EcatMasterBus::requestInit()
{
    context_.slavelist[0].state = EC_STATE_INIT;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_INIT, EC_TIMEOUTSTATE);

    if (context_.slavelist[0].state == EC_STATE_INIT)
        init_ = true;
    else
        init_ = false;
}

void EcatMasterBus::requestPreOp()
{
    context_.slavelist[0].state = EC_STATE_PRE_OP;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE);

    if (context_.slavelist[0].state == EC_STATE_PRE_OP)
        pre_op_ = true;
    else
        pre_op_ = false;
}

void EcatMasterBus::requestOperational()
{
    context_.slavelist[0].state = EC_STATE_OPERATIONAL;
    ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);

    if (context_.slavelist[0].state == EC_STATE_OPERATIONAL)
        operational_ = true;
    else
        operational_ = false;
}

bool EcatMasterBus::isOperational() const
{
    return operational_;
}

int EcatMasterBus::slaveCount() const
{
    return context_.slavecount;
}

void EcatMasterBus::cyclicTestTask()
{
    while (running_)
    {
        {
            std::lock_guard<std::mutex> lock(contextMutex_);
            ecx_send_processdata(&context_);
            wkc = ecx_receive_processdata(&context_, EC_TIMEOUTRET);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void EcatMasterBus::cyclicTask()
{
    while (running_)
    {
        {
            std::lock_guard<std::mutex> lock(contextMutex_);
            for(auto &it: slaves_)
            {
                it->updateWrite();
            }
            ecx_send_processdata(&context_);
            wkc = ecx_receive_processdata(&context_, EC_TIMEOUTRET);
            for(auto &it: slaves_)
            {
                it->updateRead();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void EcatMasterBus::checkTask()
{
    while (running_)
    {
        {
            std::lock_guard<std::mutex> lock(contextMutex_);
            if (context_.grouplist[0].docheckstate)
            {
                context_.grouplist[0].docheckstate = FALSE;
                ecx_readstate(&context_);

                for (int i = 1; i <= context_.slavecount; ++i)
                {
                    if (context_.slavelist[i].state != EC_STATE_OPERATIONAL)
                    {
                        std::cerr << "Slave "
                                  << i
                                  << " not operational\n";
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void EcatMasterBus::readTxPdo(const uint16_t slave, int size, void* buf) const {
    // std::lock_guard<std::mutex> lock(contextMutex_);
    assert(static_cast<int>(slave) <= context_.slavecount);
    assert(size == (int)context_.slavelist[slave].Ibytes);
    memcpy(buf, context_.slavelist[slave].inputs, size);
}

void EcatMasterBus::writeRxPdo(const uint16_t slave, int size, const void* buf) {
    // std::lock_guard<std::mutex> lock(contextMutex_);
    assert(static_cast<int>(slave) <= context_.slavecount);
    assert(size == (int)context_.slavelist[slave].Obytes);
    memcpy(context_.slavelist[slave].outputs, buf, size);
}

bool EcatMasterBus::addSlave(const EcatSlaveBasePtr& slave)
{
    for (const auto& existingSlave : slaves_) {
        if (slave->getAddress() == existingSlave->getAddress()) {
            std::cerr << "Slave with address " << slave->getAddress() << " already exists\n";
            return false;
        }
    }
    slaves_.push_back(slave);
    // ensure that they are sorted in adress order. this makes access simpler (access via slaveaddress -1)
    std::sort(slaves_.begin(), slaves_.end(),
              [](const EcatSlaveBasePtr& a, const EcatSlaveBasePtr& b) -> bool { return a->getAddress() < b->getAddress(); });
    return true;
}

std::string EcatMasterBus::getErrorString(ec_errort error) {
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

/*!
   * Check if an error for the SDO index of the slave exists.
   * @param slave   Address of the slave.
   * @param index   Index of the SDO.
   * @return True if an error for the index exists.
   */
bool EcatMasterBus::checkForSdoErrors(const uint16_t slave, const uint16_t index) {
    while (ecx_iserror(&context_)) {
        ec_errort error;
        if (ecx_poperror(&context_, &error)) {
            std::string errorStr = getErrorString(error);
            std::cout << errorStr;
            if (error.Slave == slave && error.Index == index) {
                return true;
            }
        }
    }
    return false;
}

bool EcatMasterBus::sdoWrite(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf) {
    int wkc = 0;
    {
        assert(static_cast<int>(slave) <= context_.slavecount);
        std::lock_guard<std::mutex> guard(contextMutex_);
        wkc = ecx_SDOwrite(&context_, slave, index, subindex, static_cast<boolean>(completeAccess), size, buf, EC_TIMEOUTRXM);
    }
    if (wkc <= 0) {
        std::cout << "Slave " << slave << ": Working counter too low (" << wkc << ") for writing SDO (ID: 0x" << std::setfill('0')
                          << std::setw(4) << std::hex << index << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                          << static_cast<uint16_t>(subindex) << ").";
        checkForSdoErrors(slave, index);
        if (slave == 0) {
            std:: cout << "[soem_interface_rsl::" << name_ << "] Worst AL status code of all slaves, alStatusCode: 0x" << std::setfill('0')
                             << std::setw(8) << std::hex << context_.slavelist[slave].ALstatuscode << " "
                             << ec_ALstatuscode2string(context_.slavelist[slave].ALstatuscode);
        } else {
            std::cout << "[soem_interface_rsl::" << name_ << "] Slave: " << slaves_[slave - 1]->getName() << " alStatusCode: 0x"
                                                     << std::setfill('0') << std::setw(8) << std::hex
                                                     << context_.slavelist[slave].ALstatuscode << " "
                                                     << ec_ALstatuscode2string(context_.slavelist[slave].ALstatuscode);
        }
        return false;
    }
    return true;
}

bool EcatMasterBus::sdoRead(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf) {
    int requestedSize = size;
    int wkc = 0;
    {
        assert(static_cast<int>(slave) <= context_.slavecount);
        std::lock_guard<std::mutex> guard(contextMutex_);
        wkc = ecx_SDOread(&context_, slave, index, subindex, static_cast<boolean>(completeAccess), &size, buf, EC_TIMEOUTRXM);
    }
    if (wkc <= 0) {
        std::cout << "Slave " << slave << ": Working counter too low (" << wkc << ") for reading SDO (ID: 0x" << std::setfill('0')
                          << std::setw(4) << std::hex << index << ", SID 0x" << std::setfill('0') << std::setw(2) << std::hex
                          << static_cast<uint16_t>(subindex) << ").";

        checkForSdoErrors(slave, index);
        if (slave == 0) {
            std::cout << "[soem_interface::" << name_ << "] Worst AL status code of all slaves, alStatusCode: 0x" << std::setfill('0')
                             << std::setw(8) << std::hex << context_.slavelist[slave].ALstatuscode << " "
                             << ec_ALstatuscode2string(context_.slavelist[slave].ALstatuscode);
        } else {
            std::cout << "[soem_interface::" << name_ << "] Slave: " << slaves_[slave - 1]->getName() << " alStatusCode: 0x"
                                                     << std::setfill('0') << std::setw(8) << std::hex
                                                     << context_.slavelist[slave].ALstatuscode << " "
                                                     << ec_ALstatuscode2string(context_.slavelist[slave].ALstatuscode);
        }
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
    if(!master_init_)
    {
        return false;
    }
    for (const auto& cfg : configs)
    {
        int ret = 0;

        if (cfg.type == SDOType::WRITE)
        {
            ret = ecx_SDOwrite(&context_, cfg.slave,
                              cfg.index,
                              cfg.subindex,
                              FALSE,
                              cfg.data.size(),
                              (void*)cfg.data.data(),
                              EC_TIMEOUTRXM);
            checkForSdoErrors(cfg.slave, cfg.index);
            printf("[SDO WRITE] slave=%d index=0x%X sub=0x%X %s\n",
                   cfg.slave, cfg.index, cfg.subindex,
                   ret > 0 ? "OK" : "FAIL");
        }
        else if (cfg.type == SDOType::READ)
        {
            std::vector<uint8_t> buffer(cfg.expected_size);
            int size = cfg.expected_size;

            ret = ecx_SDOread(&context_, cfg.slave,
                             cfg.index,
                             cfg.subindex,
                             FALSE,
                             &size,
                             buffer.data(),
                             EC_TIMEOUTRXM);
            checkForSdoErrors(cfg.slave, cfg.index);
            if (ret > 0)
            {
                printf("[SDO READ] slave=%d index=0x%X value=",
                       cfg.slave, cfg.index);

                for (int i = 0; i < size; i++)
                    printf("%02X ", buffer[i]);

                printf("\n");
            }
            else
            {
                printf("[SDO READ] FAIL slave=%d index=0x%X\n",
                       cfg.slave, cfg.index);
            }
        }


        if (ret <= 0)
        {
            return false;
        }
    }


    return true;
}
}

#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/EcatSlaveBase.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <chrono>
#include <mutex>

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

    {
        std::lock_guard<std::mutex> lock(contextMutex_);
        context_.slavelist[0].state = EC_STATE_SAFE_OP;
        ecx_writestate(&context_, 0);
        ecx_statecheck(&context_, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

        context_.slavelist[0].state = EC_STATE_INIT;
        ecx_writestate(&context_, 0);
        ecx_statecheck(&context_, 0, EC_STATE_INIT, EC_TIMEOUTSTATE);

        ecx_close(&context_);
    }
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

    return NoError;
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
}

#ifndef ECATMASTERBUS_H
#define ECATMASTERBUS_H

#include <mutex>
#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>

#include "SOEM_interface/soem_interface_export.h"
#include <SOEM_interface/SoemUtils.h>

extern "C" {
#include "soem/soem.h"
}

namespace soem_interface {

class EcatSlaveBase;
using EcatSlaveBasePtr = std::shared_ptr<EcatSlaveBase>;
using namespace error;

class SOEM_INTERFACE_EXPORT EcatMasterBus
{
public:
    explicit EcatMasterBus(const std::string& ifname = "");
    ~EcatMasterBus();

    SoemInterfaceErrorCode start();
    SoemInterfaceErrorCode startTest();
    void stop();
    void stopTest();
    bool addSlave(const EcatSlaveBasePtr& slave);

    EcatSlaveBasePtr& getSlave(const uint16_t address) { return slaves_[address - 1]; };

    void setNICName(const std::string& ifname);

    bool isOperational() const;
    int slaveCount() const;

    ecx_contextt& getContext() { return context_; }
    int getWKC() const { return wkc.load(); }

    void readTxPdo(const uint16_t slave, int size, void* buf) const;
    void writeRxPdo(const uint16_t slave, int size, const void* buf);

private:
    void cyclicTask();
    void cyclicTestTask();
    void checkTask();
    SoemInterfaceErrorCode initMaster();
    void requestOperational();

private:
    std::string nic_name_;

    std::atomic<bool> running_{false};
    std::atomic<bool> operational_{false};

    std::thread cyclicThread_;
    std::thread checkThread_;

    mutable std::mutex contextMutex_;
    ecx_contextt context_{};

    char IOmap_[4096]{};
    int expectedWKC{};
    std::atomic<int> wkc;
    // int mappingdone, dorun, inOP, dowkccheck;

    std::vector<EcatSlaveBasePtr> slaves_;
};

}

#endif

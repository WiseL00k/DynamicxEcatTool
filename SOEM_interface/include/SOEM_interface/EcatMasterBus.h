#ifndef ECATMASTERBUS_H
#define ECATMASTERBUS_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SOEM_interface/soem_interface_export.h"
#include <SOEM_interface/SoemUtils.h>

extern "C" {
#include "soem/soem.h"
}

namespace soem_interface {

class EcatSlaveBase;
using EcatSlaveBasePtr = std::shared_ptr<EcatSlaveBase>;
using namespace error;

enum SDOType
{
    WRITE,
    READ
};

struct SDOConfig
{
    uint16_t slave{0};
    uint16_t index{0};
    uint8_t subindex{0};

    SDOType type{READ};

    std::vector<uint8_t> data;
    int expected_size = 0;
};

struct ProcessDataSnapshot
{
    int workingCounter{0};
    int64_t dcTime{0};
    std::vector<uint8_t> outputPreview;
    std::vector<uint8_t> inputPreview;
};

class SOEM_INTERFACE_EXPORT EcatMasterBus
{
public:
    explicit EcatMasterBus(const std::string& ifname = "");
    ~EcatMasterBus();

    SoemInterfaceErrorCode start();
    SoemInterfaceErrorCode startTest();
    void stop();
    void stopTest() { stop(); }
    SoemInterfaceErrorCode initMaster();
    SoemInterfaceErrorCode closeMaster();
    void requestInit();
    void requestPreOp();
    void requestOperational();
    bool addSlave(const EcatSlaveBasePtr& slave);

    EcatSlaveBasePtr getSlave(uint16_t address) const;
    EcatSlaveBasePtr findSlave(uint16_t address) const;
    std::vector<EcatSlaveBasePtr> registeredSlaves() const;
    void setNICName(const std::string& ifname);

    bool isOperational() const;
    bool isMasterInitialized() const;
    int slaveCount() const;

    ecx_contextt& getContext() { return context_; }
    int getWKC() const { return wkc.load(); }
    ProcessDataSnapshot processDataSnapshot() const;

    void readTxPdo(uint16_t slave, int size, void* buf) const;
    void writeRxPdo(uint16_t slave, int size, const void* buf);
    bool sdoWrite(uint16_t slave, uint16_t index, uint8_t subindex, bool completeAccess, int size, void* buf);
    bool sdoRead(uint16_t slave, uint16_t index, uint8_t subindex, bool completeAccess, int size, void* buf);

    template <typename Value>
    bool sendSdoWrite(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, const Value value) {
        const int size = sizeof(Value);
        Value valueCopy = value;
        return sdoWrite(slave, index, subindex, completeAccess, size, &valueCopy);
    }

    template <typename Value>
    bool sendSdoRead(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, Value& value) {
        int size = sizeof(Value);
        return sdoRead(slave, index, subindex, completeAccess, size, &value);
    }
    bool applySDOConfigs(const std::vector<SDOConfig>& configs);

private:
    void cyclicTask();
    void cyclicTestTask();
    void checkTask();
    std::string getErrorString(ec_errort error);
    bool checkForSdoErrors(uint16_t slave, uint16_t index);
    bool isValidSlaveAddress(uint16_t slave) const;

private:
    std::string nic_name_;
    std::string name_{};

    std::atomic<bool> running_{false};
    std::atomic<bool> operational_{false};
    std::atomic<bool> pre_op_{false};
    std::atomic<bool> init_{false};
    std::atomic<bool> master_init_{false};

    std::thread cyclicThread_;
    std::thread checkThread_;

    mutable std::recursive_mutex contextMutex_;
    ecx_contextt context_{};

    char IOmap_[4096]{};
    int expectedWKC{};
    std::atomic<int> wkc{0};

    std::vector<EcatSlaveBasePtr> slaves_;
};

}

#endif

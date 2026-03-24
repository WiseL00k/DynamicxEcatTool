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

enum SDOType
{
    WRITE,
    READ
};

struct SDOConfig
{
    uint16_t slave;
    uint16_t index;
    uint8_t subindex;

    SDOType type;

    std::vector<uint8_t> data;   // 写用
    int expected_size = 0;       // 读用
};

class SOEM_INTERFACE_EXPORT EcatMasterBus
{
public:
    explicit EcatMasterBus(const std::string& ifname = "");
    ~EcatMasterBus();

    SoemInterfaceErrorCode start();
    SoemInterfaceErrorCode startTest();
    void stop();
    void stopTest();
    SoemInterfaceErrorCode initMaster();
    SoemInterfaceErrorCode closeMaster();
    void requestInit();
    void requestPreOp();
    void requestOperational();
    bool addSlave(const EcatSlaveBasePtr& slave);

    EcatSlaveBasePtr& getSlave(const uint16_t address) { return slaves_[address - 1]; }
    void setNICName(const std::string& ifname);

    bool isOperational() const;
    int slaveCount() const;

    ecx_contextt& getContext() { return context_; }
    int getWKC() const { return wkc.load(); }

    void readTxPdo(const uint16_t slave, int size, void* buf) const;
    void writeRxPdo(const uint16_t slave, int size, const void* buf);
    bool sdoWrite(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf);
    bool sdoRead(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf);
    /*!
   * Send a writing SDO.
   * @param slave          Address of the slave.
   * @param index          Index of the SDO.
   * @param subindex       Sub-index of the SDO.
   * @param completeAccess Access all sub-indices at once.
   * @param value          Value to write.
   * @return True if successful.
   */
    template <typename Value>
    bool sendSdoWrite(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, const Value value) {
        const int size = sizeof(Value);
        Value valueCopy = value;  // copy value to make it modifiable
        return sdoWrite(slave, index, subindex, completeAccess, size, &valueCopy);
    }

    /*!
   * Send a reading SDO.
   * @param slave          Address of the slave.
   * @param index          Index of the SDO.
   * @param subindex       Sub-index of the SDO.
   * @param completeAccess Access all sub-indices at once.
   * @param value          Return argument, will contain the value which was read.
   * @return True if successful.
   */
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
    bool checkForSdoErrors(const uint16_t slave, const uint16_t index);

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

#ifndef ECATMASTERBUS_H
#define ECATMASTERBUS_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
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

enum class PdoDirection
{
    Rx,
    Tx
};

struct SlaveIdentitySnapshot
{
    uint16_t position{0};
    std::string name;
    uint32_t vendorId{0};
    uint32_t productCode{0};
    uint32_t revision{0};
    std::optional<uint32_t> serial;
    uint32_t eepromSerial{0};
    uint16_t outputBits{0};
    uint16_t inputBits{0};
    uint16_t state{EC_STATE_NONE};
    uint16_t alStatusCode{0};
    std::string alStatusText;
};

struct ActivePdoEntry
{
    uint16_t slave{0};
    PdoDirection direction{PdoDirection::Rx};
    uint16_t pdoIndex{0};
    uint16_t index{0};
    uint8_t subindex{0};
    uint16_t dataType{0};
    uint16_t bitLength{0};
    uint32_t bitOffset{0};
    uint32_t ioMapBitOffset{0};
    std::string name;
    bool fromSii{false};
};

struct OnlineOdEntry
{
    uint16_t slave{0};
    uint16_t index{0};
    uint8_t subindex{0};
    std::string name;
    uint16_t dataType{0};
    uint16_t bitLength{0};
    uint16_t objectAccess{0};
};

struct OnlineOdResult
{
    bool success{false};
    std::string error;
    std::vector<OnlineOdEntry> entries;
};

struct SlaveStateResult
{
    uint16_t position{0};
    uint16_t requestedState{EC_STATE_NONE};
    uint16_t actualState{EC_STATE_NONE};
    uint16_t alStatusCode{0};
    std::string alStatusText;
    bool reached{false};
};

struct BusStateResult
{
    bool success{false};
    uint16_t requestedState{EC_STATE_NONE};
    uint16_t actualState{EC_STATE_NONE};
    int workingCounter{0};
    std::string error;
    std::vector<SlaveStateResult> slaves;
};

struct BusScanResult
{
    bool success{false};
    SoemInterfaceErrorCode errorCode{NoError};
    std::string error;
    int slaveCount{0};
    int ioMapSize{0};
    int expectedWorkingCounter{0};
    bool mappingReady{false};
    bool activePdoComplete{false};
    std::vector<SlaveIdentitySnapshot> slaves;
    std::vector<ActivePdoEntry> activePdos;
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
    BusScanResult scanForSlaves();
    void resetExplorer();
    void requestInit();
    void requestPreOp();
    void requestSafeOp();
    void requestOperational();
    BusStateResult requestStateDetailed(uint16_t requestedState);
    BusStateResult stateSnapshot();
    bool addSlave(const EcatSlaveBasePtr& slave);

    EcatSlaveBasePtr getSlave(uint16_t address) const;
    EcatSlaveBasePtr findSlave(uint16_t address) const;
    std::vector<EcatSlaveBasePtr> registeredSlaves() const;
    void setNICName(const std::string& ifname);

    bool isOperational() const;
    bool isMasterInitialized() const;
    bool isMappingReady() const;
    bool isProcessDataRunning() const;
    int slaveCount() const;
    int ioMapSize() const;
    int expectedWorkingCounter() const;

    ecx_contextt& getContext() { return context_; }
    int getWKC() const { return wkc.load(); }
    ProcessDataSnapshot processDataSnapshot() const;
    std::vector<SlaveIdentitySnapshot> slaveIdentities() const;
    std::vector<ActivePdoEntry> activePdoMappings() const;
    OnlineOdResult readOnlineObjectDictionary(uint16_t slave);

    bool startProcessData();
    void stopProcessData();
    bool readProcessDataRange(PdoDirection direction, uint16_t slave, size_t byteOffset,
                              size_t size, std::vector<uint8_t>& data) const;
    bool writeProcessDataRange(uint16_t slave, size_t byteOffset, const std::vector<uint8_t>& data);

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
    void checkTask();
    std::string getErrorString(ec_errort error);
    bool checkForSdoErrors(uint16_t slave, uint16_t index);
    bool isValidSlaveAddress(uint16_t slave) const;
    bool mapProcessDataLocked(std::string& error);
    bool readActivePdoMappingsLocked(std::vector<ActivePdoEntry>& mappings,
                                     std::vector<bool>& completeBySlave);
    bool readCoEPdoAssignmentCaLocked(uint16_t slave, uint16_t assignmentIndex,
                                      PdoDirection direction, std::vector<ActivePdoEntry>& mappings);
    bool readCoEPdoAssignmentLocked(uint16_t slave, uint16_t assignmentIndex,
                                    PdoDirection direction, std::vector<ActivePdoEntry>& mappings);
    bool readSiiPdoLocked(uint16_t slave, PdoDirection direction,
                          std::vector<ActivePdoEntry>& mappings);
    void updatePdoIoMapOffsetsLocked(std::vector<ActivePdoEntry>& mappings) const;
    std::vector<SlaveIdentitySnapshot> captureSlaveIdentitiesLocked(bool readSerials);
    BusStateResult writeStateLocked(uint16_t requestedState);
    BusStateResult leaveOperationalForSafeOp();
    BusStateResult currentStateResultLocked(uint16_t requestedState, const std::string& error = {}) const;
    void updateStateFlagsLocked();
    bool enableCyclicMailboxesLocked();
    void disableCyclicMailboxesLocked();
    void applyPendingPdoWritesLocked();

    struct PendingPdoWrite
    {
        uint16_t slave{0};
        size_t byteOffset{0};
        std::vector<uint8_t> data;
    };

private:
    std::string nic_name_;
    std::string name_{};

    std::atomic<bool> running_{false};
    std::atomic<bool> operational_{false};
    std::atomic<bool> pre_op_{false};
    std::atomic<bool> init_{false};
    std::atomic<bool> master_init_{false};
    std::atomic<bool> mapping_ready_{false};
    std::atomic<bool> socket_open_{false};

    std::thread cyclicThread_;
    std::thread checkThread_;

    mutable std::recursive_mutex contextMutex_;
    mutable std::mutex mailboxMutex_;
    ecx_contextt context_{};

    std::vector<uint8_t> ioMap_;
    int ioMapSize_{0};
    int expectedWKC{};
    std::atomic<int> wkc{0};
    bool registeredCallbacksEnabled_{false};

    std::vector<EcatSlaveBasePtr> slaves_;
    std::vector<SlaveIdentitySnapshot> slaveIdentities_;
    std::vector<ActivePdoEntry> activePdoMappings_;
    std::vector<bool> activePdoCompleteBySlave_;
    std::vector<PendingPdoWrite> pendingPdoWrites_;
};

}

#endif

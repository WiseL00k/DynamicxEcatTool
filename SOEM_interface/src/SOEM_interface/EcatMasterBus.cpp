#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/EcatSlaveBase.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace soem_interface {
namespace {

constexpr size_t kIoMapCapacity =
    static_cast<size_t>(EC_MAXIOSEGMENTS) * static_cast<size_t>(EC_MAXLRWDATA);

uint16_t baseState(const uint16_t state)
{
    return state & 0x0f;
}

bool isSupportedState(const uint16_t state)
{
    return state == EC_STATE_INIT || state == EC_STATE_PRE_OP ||
           state == EC_STATE_SAFE_OP || state == EC_STATE_OPERATIONAL;
}

bool stateReached(const uint16_t actual, const uint16_t requested)
{
    return baseState(actual) == requested && (actual & EC_STATE_ERROR) == 0;
}

std::string alStatusText(const uint16_t code)
{
    const char* text = ec_ALstatuscode2string(code);
    return text != nullptr ? std::string(text) : std::string();
}

} // namespace

EcatMasterBus::EcatMasterBus(const std::string& ifname)
    : nic_name_(ifname), ioMap_(kIoMapCapacity, 0)
{}

EcatMasterBus::~EcatMasterBus()
{
    stop();
}

void EcatMasterBus::setNICName(const std::string& ifname)
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    if (!socket_open_) {
        nic_name_ = ifname;
    }
}

SoemInterfaceErrorCode EcatMasterBus::startTest()
{
    if (running_) {
        return NoError;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(contextMutex_);
        if (nic_name_.empty()) {
            return InvalidNicName;
        }
        registeredCallbacksEnabled_ = false;
    }

    const SoemInterfaceErrorCode errorCode = initMaster();
    if (errorCode != NoError) {
        return errorCode;
    }

    const BusStateResult stateResult = requestStateDetailed(EC_STATE_OPERATIONAL);
    if (!stateResult.success) {
        closeMaster();
        return RequestOpFailed;
    }

    return NoError;
}

SoemInterfaceErrorCode EcatMasterBus::start()
{
    if (running_) {
        return NoError;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(contextMutex_);
        if (nic_name_.empty()) {
            return InvalidNicName;
        }
    }

    const SoemInterfaceErrorCode errorCode = initMaster();
    if (errorCode != NoError) {
        return errorCode;
    }

    const auto slaves = registeredSlaves();
    for (const auto& slave : slaves) {
        if (!slave || !slave->startup()) {
            closeMaster();
            return InvalidSlave;
        }

        std::lock_guard<std::recursive_mutex> lock(contextMutex_);
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

    {
        std::lock_guard<std::recursive_mutex> lock(contextMutex_);
        registeredCallbacksEnabled_ = true;
    }

    const BusStateResult stateResult = requestStateDetailed(EC_STATE_OPERATIONAL);
    if (!stateResult.success) {
        closeMaster();
        return RequestOpFailed;
    }

    return NoError;
}

void EcatMasterBus::stop()
{
    closeMaster();
}

BusScanResult EcatMasterBus::scanForSlaves()
{
    resetExplorer();

    BusScanResult result;
    {
        std::unique_lock<std::mutex> mailboxLock(mailboxMutex_);
        std::unique_lock<std::recursive_mutex> contextLock(contextMutex_);

        if (nic_name_.empty()) {
            result.errorCode = InvalidNicName;
            result.error = "The network adapter name is empty.";
            return result;
        }

        context_.manualstatechange = 1;
        if (ecx_init(&context_, nic_name_.c_str()) <= 0) {
            result.errorCode = EcatInitFailed;
            result.error = "SOEM could not open the selected network adapter.";
            return result;
        }
        socket_open_ = true;

        context_.manualstatechange = 1;
        const int discovered = ecx_config_init(&context_);
        if (discovered <= 0) {
            result.errorCode = NoSlaveFound;
            result.error = "No EtherCAT slaves were found.";
            contextLock.unlock();
            mailboxLock.unlock();
            closeMaster();
            return result;
        }

        master_init_ = true;
        context_.manualstatechange = 1;

        const BusStateResult preOpResult = writeStateLocked(EC_STATE_PRE_OP);
        if (!preOpResult.success) {
            result.errorCode = EcatInitFailed;
            result.error = preOpResult.error.empty()
                ? "Not all slaves reached PRE-OP after discovery."
                : preOpResult.error;
            contextLock.unlock();
            mailboxLock.unlock();
            closeMaster();
            return result;
        }

        slaveIdentities_ = captureSlaveIdentitiesLocked(true);

        std::string mappingError;
        if (!mapProcessDataLocked(mappingError)) {
            result.errorCode = EcatInitFailed;
            result.error = mappingError;
            contextLock.unlock();
            mailboxLock.unlock();
            closeMaster();
            return result;
        }

        readActivePdoMappingsLocked(activePdoMappings_, activePdoCompleteBySlave_);
        ecx_configdc(&context_);
        updatePdoIoMapOffsetsLocked(activePdoMappings_);
        ecx_readstate(&context_);
        updateStateFlagsLocked();

        bool allMappingsComplete = true;
        for (int slave = 1; slave <= context_.slavecount; ++slave) {
            uint32_t outputBits = 0;
            uint32_t inputBits = 0;
            for (const ActivePdoEntry& entry : activePdoMappings_) {
                if (entry.slave != slave) {
                    continue;
                }
                if (entry.direction == PdoDirection::Rx) {
                    outputBits += entry.bitLength;
                } else {
                    inputBits += entry.bitLength;
                }
            }

            bool complete = static_cast<size_t>(slave) < activePdoCompleteBySlave_.size() &&
                            activePdoCompleteBySlave_[static_cast<size_t>(slave)];
            complete = complete &&
                       outputBits == context_.slavelist[slave].Obits &&
                       inputBits == context_.slavelist[slave].Ibits;
            if (context_.slavelist[slave].Obits == 0 &&
                context_.slavelist[slave].Ibits == 0) {
                complete = true;
            }
            if (static_cast<size_t>(slave) < activePdoCompleteBySlave_.size()) {
                activePdoCompleteBySlave_[static_cast<size_t>(slave)] = complete;
            }
            allMappingsComplete = allMappingsComplete && complete;

            SlaveIdentitySnapshot& identity = slaveIdentities_[static_cast<size_t>(slave - 1)];
            identity.outputBits = context_.slavelist[slave].Obits;
            identity.inputBits = context_.slavelist[slave].Ibits;
            identity.state = context_.slavelist[slave].state;
            identity.alStatusCode = context_.slavelist[slave].ALstatuscode;
            identity.alStatusText = alStatusText(identity.alStatusCode);
        }

        result.success = true;
        result.errorCode = NoError;
        result.slaveCount = context_.slavecount;
        result.ioMapSize = ioMapSize_;
        result.expectedWorkingCounter = expectedWKC;
        result.mappingReady = mapping_ready_;
        result.activePdoComplete = allMappingsComplete;
        result.slaves = slaveIdentities_;
        result.activePdos = activePdoMappings_;
    }

    return result;
}

SoemInterfaceErrorCode EcatMasterBus::initMaster()
{
    if (master_init_) {
        return NoError;
    }

    const BusScanResult scanResult = scanForSlaves();
    if (!scanResult.success) {
        return scanResult.errorCode;
    }

    const BusStateResult stateResult = requestStateDetailed(EC_STATE_SAFE_OP);
    if (!stateResult.success) {
        closeMaster();
        return EcatInitFailed;
    }
    return NoError;
}

SoemInterfaceErrorCode EcatMasterBus::closeMaster()
{
    if (socket_open_ && operational_) {
        requestStateDetailed(EC_STATE_SAFE_OP);
    }

    stopProcessData();

    std::lock_guard<std::mutex> mailboxLock(mailboxMutex_);
    std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
    if (!socket_open_) {
        return NoError;
    }

    disableCyclicMailboxesLocked();
    if (context_.slavecount > 0) {
        writeStateLocked(EC_STATE_INIT);
    }
    ecx_close(&context_);
    std::memset(&context_, 0, sizeof(context_));

    socket_open_ = false;
    master_init_ = false;
    mapping_ready_ = false;
    operational_ = false;
    pre_op_ = false;
    init_ = false;
    ioMapSize_ = 0;
    expectedWKC = 0;
    wkc = 0;
    pendingPdoWrites_.clear();
    return NoError;
}

void EcatMasterBus::resetExplorer()
{
    closeMaster();

    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    slaveIdentities_.clear();
    activePdoMappings_.clear();
    activePdoCompleteBySlave_.clear();
    pendingPdoWrites_.clear();
    registeredCallbacksEnabled_ = false;
    std::fill(ioMap_.begin(), ioMap_.end(), 0);
}

void EcatMasterBus::requestInit()
{
    requestStateDetailed(EC_STATE_INIT);
}

void EcatMasterBus::requestPreOp()
{
    requestStateDetailed(EC_STATE_PRE_OP);
}

void EcatMasterBus::requestSafeOp()
{
    requestStateDetailed(EC_STATE_SAFE_OP);
}

void EcatMasterBus::requestOperational()
{
    requestStateDetailed(EC_STATE_OPERATIONAL);
}

BusStateResult EcatMasterBus::leaveOperationalForSafeOp()
{
    BusStateResult result;
    std::unique_lock<std::mutex> mailboxLock(mailboxMutex_);
    int stateWriteWkc = 0;
    {
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        if (!master_init_) {
            result.requestedState = EC_STATE_SAFE_OP;
            result.error = "The EtherCAT bus was closed during the state request.";
        } else {
            context_.slavelist[0].state = EC_STATE_SAFE_OP;
            stateWriteWkc = ecx_writestate(&context_, 0);
            if (stateWriteWkc <= 0) {
                result = currentStateResultLocked(
                    EC_STATE_SAFE_OP, "SOEM could not write the SAFE-OP state request.");
            }
        }
    }

    if (master_init_ && stateWriteWkc > 0) {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::microseconds(EC_TIMEOUTSTATE);
        do {
            {
                std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
                ecx_readstate(&context_);
                updateStateFlagsLocked();
                result = currentStateResultLocked(EC_STATE_SAFE_OP);
                if (result.success) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
    }

    // Leaving OP always terminates the OP cycle, even when the AL-state
    // request failed or timed out. Keeping these threads alive makes the next
    // OP request reuse stale cyclic-mailbox/check state and can deadlock.
    mailboxLock.unlock();
    stopProcessData();
    return result;
}
BusStateResult EcatMasterBus::requestStateDetailed(const uint16_t requestedState)
{
    if (!isSupportedState(requestedState)) {
        BusStateResult result;
        result.requestedState = requestedState;
        result.error = "Unsupported EtherCAT state request.";
        return result;
    }

    if (!master_init_) {
        BusStateResult result;
        result.requestedState = requestedState;
        result.error = "The EtherCAT bus has not been scanned.";
        return result;
    }

    if (requestedState != EC_STATE_OPERATIONAL && operational_) {
        const BusStateResult safeResult = leaveOperationalForSafeOp();
        if (!safeResult.success) {
            return safeResult;
        }
        if (requestedState == EC_STATE_SAFE_OP) {
            return safeResult;
        }
    } else if (requestedState != EC_STATE_OPERATIONAL && running_) {
        stopProcessData();
    }

    BusStateResult result;
    bool transitionToOperational = false;
    {
        std::lock_guard<std::mutex> mailboxLock(mailboxMutex_);
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);

        if (!master_init_) {
            result.requestedState = requestedState;
            result.error = "The EtherCAT bus was closed during the state request.";
            return result;
        }

        uint16_t current = baseState(context_.slavelist[0].state);
        if (current == EC_STATE_INIT && requestedState != EC_STATE_INIT) {
            return currentStateResultLocked(
                requestedState,
                "The bus entered INIT. Reset and rescan before requesting a higher state.");
        }
        if (requestedState == EC_STATE_INIT) {
            result = writeStateLocked(EC_STATE_INIT);
            mapping_ready_ = false;
            pendingPdoWrites_.clear();
            return result;
        }

        if (current == EC_STATE_INIT) {
            result = writeStateLocked(EC_STATE_PRE_OP);
            if (!result.success || requestedState == EC_STATE_PRE_OP) {
                return result;
            }
            current = EC_STATE_PRE_OP;
        }

        if ((requestedState == EC_STATE_SAFE_OP ||
             requestedState == EC_STATE_OPERATIONAL) && !mapping_ready_) {
            std::string mappingError;
            if (!mapProcessDataLocked(mappingError)) {
                return currentStateResultLocked(
                    requestedState, mappingError);
            }
            readActivePdoMappingsLocked(
                activePdoMappings_, activePdoCompleteBySlave_);
            ecx_configdc(&context_);
            updatePdoIoMapOffsetsLocked(activePdoMappings_);
        }

        if (requestedState == EC_STATE_PRE_OP) {
            return writeStateLocked(EC_STATE_PRE_OP);
        }

        if (current != EC_STATE_SAFE_OP && current != EC_STATE_OPERATIONAL) {
            result = writeStateLocked(EC_STATE_SAFE_OP);
            if (!result.success || requestedState == EC_STATE_SAFE_OP) {
                return result;
            }
        } else if (requestedState == EC_STATE_SAFE_OP) {
            return writeStateLocked(EC_STATE_SAFE_OP);
        }

        if (requestedState == EC_STATE_OPERATIONAL) {
            if (registeredCallbacksEnabled_) {
                enableCyclicMailboxesLocked();
            } else {
                disableCyclicMailboxesLocked();
            }
            const int sendWkc = ecx_send_processdata(&context_);
            const int receiveWkc = sendWkc > 0
                ? ecx_receive_processdata(&context_, EC_TIMEOUTRET)
                : -1;
            wkc = receiveWkc;
            const bool validWkc =
                receiveWkc >= expectedWKC && receiveWkc >= 0;
            if (!validWkc) {
                std::ostringstream message;
                message << "Initial process-data exchange failed (WKC "
                        << receiveWkc << ", expected " << expectedWKC << ").";
                disableCyclicMailboxesLocked();
                return currentStateResultLocked(
                    requestedState, message.str());
            }
            transitionToOperational = true;
        }
    }

    if (!transitionToOperational) {
        return result;
    }
    if (!startProcessData()) {
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        return currentStateResultLocked(
            requestedState, "The process-data thread could not be started.");
    }

    std::unique_lock<std::mutex> mailboxLock(mailboxMutex_);
    int stateWriteWkc = 0;
    {
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        context_.slavelist[0].state = EC_STATE_OPERATIONAL;
        stateWriteWkc = ecx_writestate(&context_, 0);
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::microseconds(EC_TIMEOUTSTATE);
    do {
        {
            std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
            ecx_readstate(&context_);
            updateStateFlagsLocked();
            result = currentStateResultLocked(requestedState);
            if (stateWriteWkc > 0 && result.success) {
                return result;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);

    {
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        result = currentStateResultLocked(
            requestedState,
            stateWriteWkc <= 0
                ? "SOEM could not write the OP state request."
                : "Not all slaves reached OP before the timeout.");
        writeStateLocked(EC_STATE_SAFE_OP);
    }
    mailboxLock.unlock();
    stopProcessData();
    return result;
}

BusStateResult EcatMasterBus::stateSnapshot()
{
    std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
    if (!master_init_ || context_.slavecount <= 0) {
        BusStateResult result;
        result.error = "The EtherCAT bus has not been scanned.";
        return result;
    }
    ecx_readstate(&context_);
    updateStateFlagsLocked();
    const uint16_t requested = baseState(context_.slavelist[0].state);
    return currentStateResultLocked(requested);
}


bool EcatMasterBus::isOperational() const
{
    return operational_;
}

bool EcatMasterBus::isMasterInitialized() const
{
    return master_init_;
}

bool EcatMasterBus::isMappingReady() const
{
    return mapping_ready_;
}

bool EcatMasterBus::isProcessDataRunning() const
{
    return running_;
}

int EcatMasterBus::slaveCount() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    return context_.slavecount;
}

int EcatMasterBus::ioMapSize() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    return ioMapSize_;
}

int EcatMasterBus::expectedWorkingCounter() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    return expectedWKC;
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
    const size_t outputPreviewSize = std::min<size_t>(group->Obytes, 8);
    const size_t inputPreviewSize = std::min<size_t>(group->Ibytes, 8);
    if (outputPreviewSize > 0 && context_.slavelist[0].outputs != nullptr) {
        snapshot.outputPreview.assign(
            context_.slavelist[0].outputs,
            context_.slavelist[0].outputs + outputPreviewSize);
    }
    if (inputPreviewSize > 0 && context_.slavelist[0].inputs != nullptr) {
        snapshot.inputPreview.assign(
            context_.slavelist[0].inputs,
            context_.slavelist[0].inputs + inputPreviewSize);
    }

    return snapshot;
}

std::vector<SlaveIdentitySnapshot> EcatMasterBus::slaveIdentities() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    return slaveIdentities_;
}

std::vector<ActivePdoEntry> EcatMasterBus::activePdoMappings() const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    return activePdoMappings_;
}

bool EcatMasterBus::startProcessData()
{
    std::lock_guard<std::mutex> mailboxLock(mailboxMutex_);
    std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
    if (running_) {
        return true;
    }
    const uint16_t state = baseState(context_.slavelist[0].state);
    if (!master_init_ || !mapping_ready_ ||
        (state != EC_STATE_SAFE_OP && state != EC_STATE_OPERATIONAL)) {
        return false;
    }

    if (registeredCallbacksEnabled_) {
        enableCyclicMailboxesLocked();
    } else {
        disableCyclicMailboxesLocked();
    }
    running_ = true;
    try {
        cyclicThread_ = std::thread(&EcatMasterBus::cyclicTask, this);
        if (registeredCallbacksEnabled_) {
            checkThread_ = std::thread(&EcatMasterBus::checkTask, this);
        }
    } catch (...) {
        running_ = false;
        if (cyclicThread_.joinable()) {
            cyclicThread_.join();
        }
        if (checkThread_.joinable()) {
            checkThread_.join();
        }
        disableCyclicMailboxesLocked();
        return false;
    }
    return true;
}

void EcatMasterBus::stopProcessData()
{
    std::lock_guard<std::mutex> mailboxLock(mailboxMutex_);
    running_ = false;

    if (cyclicThread_.joinable()) {
        cyclicThread_.join();
    }
    if (checkThread_.joinable()) {
        checkThread_.join();
    }

    std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
    disableCyclicMailboxesLocked();
}

void EcatMasterBus::cyclicTask()
{
    while (running_) {
        {
            std::lock_guard<std::recursive_mutex> lock(contextMutex_);
            if (registeredCallbacksEnabled_) {
                for (auto& slave : slaves_) {
                    if (slave) {
                        slave->updateWrite();
                    }
                }
            }

            applyPendingPdoWritesLocked();
            ecx_send_processdata(&context_);
            wkc = ecx_receive_processdata(&context_, EC_TIMEOUTRET);

            if (registeredCallbacksEnabled_) {
                for (auto& slave : slaves_) {
                    if (slave) {
                        slave->updateRead();
                    }
                }
            }
            if (registeredCallbacksEnabled_) {
                ecx_mbxhandler(&context_, 0, 4);
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
            const bool workingCounterLow =
                expectedWKC > 0 && wkc.load() < expectedWKC;
            if (workingCounterLow || context_.grouplist[0].docheckstate) {
                context_.grouplist[0].docheckstate = FALSE;
                ecx_readstate(&context_);
                updateStateFlagsLocked();

                for (int i = 1; i <= context_.slavecount; ++i) {
                    if (!stateReached(context_.slavelist[i].state, EC_STATE_OPERATIONAL)) {
                        std::cerr << "Slave " << i << " not operational: "
                                  << alStatusText(context_.slavelist[i].ALstatuscode) << '\n';
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

bool EcatMasterBus::readProcessDataRange(
    const PdoDirection direction,
    const uint16_t slave,
    const size_t byteOffset,
    const size_t size,
    std::vector<uint8_t>& data) const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    if (!mapping_ready_ || !isValidSlaveAddress(slave)) {
        return false;
    }

    const ec_slavet& item = context_.slavelist[slave];
    const uint8_t* source =
        direction == PdoDirection::Rx ? item.outputs : item.inputs;
    const size_t availableBits =
        direction == PdoDirection::Rx ? item.Obits : item.Ibits;
    const size_t availableBytes = (availableBits + 7u) / 8u;
    if (source == nullptr || byteOffset > availableBytes ||
        size > availableBytes - byteOffset) {
        return false;
    }

    data.assign(source + byteOffset, source + byteOffset + size);
    if (direction == PdoDirection::Rx && !pendingPdoWrites_.empty()) {
        const size_t requestedBegin = byteOffset;
        const size_t requestedEnd = byteOffset + size;
        for (const PendingPdoWrite& command : pendingPdoWrites_) {
            if (command.slave != slave || command.data.empty()) {
                continue;
            }
            const size_t commandBegin = command.byteOffset;
            const size_t commandEnd = command.byteOffset + command.data.size();
            const size_t overlapBegin = (std::max)(requestedBegin, commandBegin);
            const size_t overlapEnd = (std::min)(requestedEnd, commandEnd);
            if (overlapBegin >= overlapEnd) {
                continue;
            }
            std::copy(command.data.begin() + (overlapBegin - commandBegin),
                      command.data.begin() + (overlapEnd - commandBegin),
                      data.begin() + (overlapBegin - requestedBegin));
        }
    }


    return true;
}

bool EcatMasterBus::writeProcessDataRange(
    const uint16_t slave,
    const size_t byteOffset,
    const std::vector<uint8_t>& data)
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    if (!mapping_ready_ || !isValidSlaveAddress(slave)) {
        return false;
    }

    ec_slavet& item = context_.slavelist[slave];
    const size_t availableBytes = (static_cast<size_t>(item.Obits) + 7u) / 8u;
    if (item.outputs == nullptr || byteOffset > availableBytes ||
        data.size() > availableBytes - byteOffset) {
        return false;
    }

    if (running_) {
        pendingPdoWrites_.push_back(PendingPdoWrite{slave, byteOffset, data});
    } else if (!data.empty()) {
        std::memcpy(item.outputs + byteOffset, data.data(), data.size());
    }
    return true;
}

void EcatMasterBus::applyPendingPdoWritesLocked()
{
    for (const PendingPdoWrite& command : pendingPdoWrites_) {
        if (!isValidSlaveAddress(command.slave)) {
            continue;
        }

        ec_slavet& item = context_.slavelist[command.slave];
        const size_t availableBytes = (static_cast<size_t>(item.Obits) + 7u) / 8u;
        if (item.outputs != nullptr && command.byteOffset <= availableBytes &&
            command.data.size() <= availableBytes - command.byteOffset &&
            !command.data.empty()) {
            std::memcpy(item.outputs + command.byteOffset,
                        command.data.data(), command.data.size());
        }
    }
    pendingPdoWrites_.clear();
}

void EcatMasterBus::readTxPdo(const uint16_t slave, int size, void* buf) const
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    assert(isValidSlaveAddress(slave));
    if (!isValidSlaveAddress(slave)) {
        return;
    }

    assert(size == static_cast<int>(context_.slavelist[slave].Ibytes));
    if (size != static_cast<int>(context_.slavelist[slave].Ibytes) ||
        context_.slavelist[slave].inputs == nullptr || buf == nullptr) {
        return;
    }

    std::memcpy(buf, context_.slavelist[slave].inputs, static_cast<size_t>(size));
}

void EcatMasterBus::writeRxPdo(const uint16_t slave, int size, const void* buf)
{
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    assert(isValidSlaveAddress(slave));
    if (!isValidSlaveAddress(slave)) {
        return;
    }

    assert(size == static_cast<int>(context_.slavelist[slave].Obytes));
    if (size != static_cast<int>(context_.slavelist[slave].Obytes) ||
        context_.slavelist[slave].outputs == nullptr || buf == nullptr) {
        return;
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    std::memcpy(data.data(), buf, static_cast<size_t>(size));
    if (running_) {
        pendingPdoWrites_.push_back(PendingPdoWrite{slave, 0, std::move(data)});
    } else {
        std::memcpy(context_.slavelist[slave].outputs, buf, static_cast<size_t>(size));
    }
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

bool EcatMasterBus::mapProcessDataLocked(std::string& error)
{
    error.clear();
    if (!master_init_ || context_.slavecount <= 0) {
        error = "The EtherCAT bus is not initialized.";
        return false;
    }
    if (ioMap_.size() < kIoMapCapacity || ioMap_.empty()) {
        error = "The process image buffer is not available.";
        return false;
    }

    context_.manualstatechange = 1;
    std::fill(ioMap_.begin(), ioMap_.end(), 0);
    const int mappedSize = ecx_config_map_group(&context_, ioMap_.data(), 0);
    if (mappedSize < 0 || static_cast<size_t>(mappedSize) > ioMap_.size()) {
        error = "SOEM returned an invalid process image size.";
        mapping_ready_ = false;
        return false;
    }

    const uintptr_t mapBegin = reinterpret_cast<uintptr_t>(ioMap_.data());
    const uintptr_t mapEnd = mapBegin + static_cast<size_t>(mappedSize);
    const auto pointerWithinMap = [mapBegin, mapEnd](
        const uint8_t* pointer, const size_t span) {
        if (span == 0) {
            return pointer == nullptr ||
                   (reinterpret_cast<uintptr_t>(pointer) >= mapBegin &&
                    reinterpret_cast<uintptr_t>(pointer) <= mapEnd);
        }
        if (pointer == nullptr) {
            return false;
        }
        const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
        return begin >= mapBegin && begin <= mapEnd &&
               span <= mapEnd - begin;
    };

    for (int slave = 1; slave <= context_.slavecount; ++slave) {
        const ec_slavet& item = context_.slavelist[slave];
        const size_t outputBytes = (static_cast<size_t>(item.Obits) + 7u) / 8u;
        const size_t inputBytes = (static_cast<size_t>(item.Ibits) + 7u) / 8u;
        if (!pointerWithinMap(item.outputs, outputBytes) ||
            !pointerWithinMap(item.inputs, inputBytes)) {
            std::ostringstream message;
            message << "Slave " << slave
                    << " has a process image pointer outside the allocated IOmap.";
            error = message.str();
            mapping_ready_ = false;
            return false;
        }
    }

    ioMapSize_ = mappedSize;
    const ec_groupt& group = context_.grouplist[0];
    expectedWKC = (static_cast<int>(group.outputsWKC) * 2) +
                  static_cast<int>(group.inputsWKC);
    mapping_ready_ = true;
    pendingPdoWrites_.clear();
    return true;
}

bool EcatMasterBus::readCoEPdoAssignmentCaLocked(
    const uint16_t slave,
    const uint16_t assignmentIndex,
    const PdoDirection direction,
    std::vector<ActivePdoEntry>& mappings)
{
    const size_t originalSize = mappings.size();
    ec_PDOassignt assignment{};
    int size = sizeof(assignment);
    if (ecx_SDOread(&context_, slave, assignmentIndex, 0, TRUE,
                    &size, &assignment, EC_TIMEOUTRXM) <= 0 ||
        assignment.n == 0 ||
        size < static_cast<int>(2u + static_cast<size_t>(assignment.n) * sizeof(uint16_t))) {
        checkForSdoErrors(slave, assignmentIndex);
        return false;
    }

    uint32_t bitOffset = 0;
    for (uint16_t assignmentSubindex = 0;
         assignmentSubindex < assignment.n; ++assignmentSubindex) {
        const uint16_t pdoIndex = etohs(assignment.index[assignmentSubindex]);
        if (pdoIndex == 0) {
            continue;
        }

        ec_PDOdesct description{};
        size = sizeof(description);
        if (ecx_SDOread(&context_, slave, pdoIndex, 0, TRUE,
                        &size, &description, EC_TIMEOUTRXM) <= 0 ||
            description.n == 0 ||
            size < static_cast<int>(2u + static_cast<size_t>(description.n) * sizeof(uint32_t))) {
            checkForSdoErrors(slave, pdoIndex);
            mappings.resize(originalSize);
            return false;
        }

        for (uint16_t mappingSubindex = 0;
             mappingSubindex < description.n; ++mappingSubindex) {
            const uint32_t mapping = etohl(description.PDO[mappingSubindex]);
            const uint16_t bitLength = static_cast<uint16_t>(mapping & 0xffu);
            if (bitLength == 0) {
                mappings.resize(originalSize);
                return false;
            }

            ActivePdoEntry entry;
            entry.slave = slave;
            entry.direction = direction;
            entry.pdoIndex = pdoIndex;
            entry.index = static_cast<uint16_t>((mapping >> 16u) & 0xffffu);
            entry.subindex = static_cast<uint8_t>((mapping >> 8u) & 0xffu);
            entry.bitLength = bitLength;
            entry.bitOffset = bitOffset;
            mappings.push_back(std::move(entry));
            bitOffset += bitLength;
        }
    }

    if (mappings.size() == originalSize) {
        return false;
    }
    return true;
}

bool EcatMasterBus::readCoEPdoAssignmentLocked(
    const uint16_t slave,
    const uint16_t assignmentIndex,
    const PdoDirection direction,
    std::vector<ActivePdoEntry>& mappings)
{
    const size_t originalSize = mappings.size();
    uint8_t rawAssignmentCount[sizeof(uint16_t)]{};
    int size = sizeof(rawAssignmentCount);
    if (ecx_SDOread(&context_, slave, assignmentIndex, 0, FALSE,
                    &size, rawAssignmentCount, EC_TIMEOUTRXM) <= 0 ||
        (size != 1 && size != static_cast<int>(sizeof(rawAssignmentCount)))) {
        checkForSdoErrors(slave, assignmentIndex);
        return false;
    }

    const uint16_t assignmentCount = static_cast<uint16_t>(rawAssignmentCount[0]) |
        (size == 2 ? static_cast<uint16_t>(rawAssignmentCount[1]) << 8u : 0u);

    uint32_t bitOffset = 0;
    for (uint16_t assignmentSubindex = 1;
         assignmentSubindex <= assignmentCount; ++assignmentSubindex) {
        uint16_t rawPdoIndex = 0;
        size = sizeof(rawPdoIndex);
        if (ecx_SDOread(&context_, slave, assignmentIndex,
                        static_cast<uint8_t>(assignmentSubindex), FALSE,
                        &size, &rawPdoIndex, EC_TIMEOUTRXM) <= 0 ||
            size != static_cast<int>(sizeof(rawPdoIndex))) {
            checkForSdoErrors(slave, assignmentIndex);
            mappings.resize(originalSize);
            return false;
        }

        const uint16_t pdoIndex = etohs(rawPdoIndex);
        if (pdoIndex == 0) {
            continue;
        }

        uint8_t entryCount = 0;
        size = sizeof(entryCount);
        if (ecx_SDOread(&context_, slave, pdoIndex, 0, FALSE,
                        &size, &entryCount, EC_TIMEOUTRXM) <= 0 ||
            size != static_cast<int>(sizeof(entryCount))) {
            checkForSdoErrors(slave, pdoIndex);
            mappings.resize(originalSize);
            return false;
        }

        for (uint16_t mappingSubindex = 1;
             mappingSubindex <= entryCount; ++mappingSubindex) {
            uint32_t rawMapping = 0;
            size = sizeof(rawMapping);
            if (ecx_SDOread(&context_, slave, pdoIndex,
                            static_cast<uint8_t>(mappingSubindex), FALSE,
                            &size, &rawMapping, EC_TIMEOUTRXM) <= 0 ||
                size != static_cast<int>(sizeof(rawMapping))) {
                checkForSdoErrors(slave, pdoIndex);
                mappings.resize(originalSize);
                return false;
            }

            const uint32_t mapping = etohl(rawMapping);
            const uint16_t bitLength = static_cast<uint16_t>(mapping & 0xffu);
            if (bitLength == 0) {
                mappings.resize(originalSize);
                return false;
            }

            ActivePdoEntry entry;
            entry.slave = slave;
            entry.direction = direction;
            entry.pdoIndex = pdoIndex;
            entry.index = static_cast<uint16_t>((mapping >> 16u) & 0xffffu);
            entry.subindex = static_cast<uint8_t>((mapping >> 8u) & 0xffu);
            entry.bitLength = bitLength;
            entry.bitOffset = bitOffset;
            mappings.push_back(std::move(entry));
            bitOffset += bitLength;
        }
    }
    return true;
}

bool EcatMasterBus::readSiiPdoLocked(
    const uint16_t slave,
    const PdoDirection direction,
    std::vector<ActivePdoEntry>& mappings)
{
    const uint8_t sectionType =
        direction == PdoDirection::Rx ? 1u : 0u;
    const uint8_t eepromWasPdi = context_.slavelist[slave].eep_pdi;
    const uint16_t startPosition =
        ecx_siifind(&context_, slave, ECT_SII_PDO + sectionType);
    if (startPosition == 0) {
        if (eepromWasPdi) {
            ecx_eeprom2pdi(&context_, slave);
        }
        return false;
    }

    uint16_t address = startPosition;
    uint16_t sectionLength = ecx_siigetbyte(&context_, slave, address++);
    sectionLength +=
        static_cast<uint16_t>(ecx_siigetbyte(&context_, slave, address++)) << 8u;

    uint16_t consumedWords = 1;
    uint32_t bitOffset = 0;
    while (consumedWords < sectionLength) {
        const uint16_t pdoIndex =
            static_cast<uint16_t>(ecx_siigetbyte(&context_, slave, address++)) |
            (static_cast<uint16_t>(ecx_siigetbyte(&context_, slave, address++)) << 8u);
        const uint8_t entryCount = ecx_siigetbyte(&context_, slave, address++);
        const uint8_t syncManager = ecx_siigetbyte(&context_, slave, address++);
        address++;
        const uint8_t pdoNameIndex = ecx_siigetbyte(&context_, slave, address++);
        address += 2;
        consumedWords += 4;

        std::string pdoName;
        if (pdoNameIndex != 0) {
            char name[EC_MAXNAME + 1]{};
            ecx_siistring(&context_, name, slave, pdoNameIndex);
            pdoName = name;
        }

        const bool active = syncManager < EC_MAXSM;
        for (uint16_t entryNumber = 0;
             entryNumber < entryCount; ++entryNumber) {
            const uint16_t objectIndex =
                static_cast<uint16_t>(ecx_siigetbyte(&context_, slave, address++)) |
                (static_cast<uint16_t>(ecx_siigetbyte(&context_, slave, address++)) << 8u);
            const uint8_t objectSubindex =
                ecx_siigetbyte(&context_, slave, address++);
            const uint8_t nameIndex =
                ecx_siigetbyte(&context_, slave, address++);
            const uint8_t dataType =
                ecx_siigetbyte(&context_, slave, address++);
            const uint8_t bitLength =
                ecx_siigetbyte(&context_, slave, address++);
            address += 2;
            consumedWords += 4;

            if (!active || bitLength == 0) {
                continue;
            }

            ActivePdoEntry entry;
            entry.slave = slave;
            entry.direction = direction;
            entry.pdoIndex = pdoIndex;
            entry.index = objectIndex;
            entry.subindex = objectSubindex;
            entry.dataType = dataType;
            entry.bitLength = bitLength;
            entry.bitOffset = bitOffset;
            entry.name = pdoName;
            entry.fromSii = true;
            if (nameIndex != 0) {
                char name[EC_MAXNAME + 1]{};
                ecx_siistring(&context_, name, slave, nameIndex);
                entry.name = name;
            }
            mappings.push_back(std::move(entry));
            bitOffset += bitLength;
        }
    }

    if (eepromWasPdi) {
        ecx_eeprom2pdi(&context_, slave);
    }
    return true;
}

bool EcatMasterBus::readActivePdoMappingsLocked(
    std::vector<ActivePdoEntry>& mappings,
    std::vector<bool>& completeBySlave)
{
    mappings.clear();
    completeBySlave.assign(
        static_cast<size_t>((std::max)(context_.slavecount, 0)) + 1u, false);

    bool allComplete = true;
    for (int slaveIndex = 1; slaveIndex <= context_.slavecount; ++slaveIndex) {
        const uint16_t slave = static_cast<uint16_t>(slaveIndex);
        const bool hasCoE =
            (context_.slavelist[slave].mbx_proto & ECT_MBXPROT_COE) != 0;

        const auto readDirection = [&](const PdoDirection direction) {
            const uint32_t expectedBits = direction == PdoDirection::Rx
                ? context_.slavelist[slave].Obits
                : context_.slavelist[slave].Ibits;
            if (expectedBits == 0) {
                return true;
            }

            const size_t directionStart = mappings.size();
            const auto hasExpectedBits = [&]() {
                uint32_t mappedBits = 0;
                for (size_t index = directionStart; index < mappings.size(); ++index) {
                    mappedBits += mappings[index].bitLength;
                }
                return mappedBits == expectedBits;
            };

            if (hasCoE) {
                const uint16_t assignment =
                    direction == PdoDirection::Rx ? 0x1c12u : 0x1c13u;
                const bool supportsCompleteAccess =
                    (context_.slavelist[slave].CoEdetails & ECT_COEDET_SDOCA) != 0;

                if (supportsCompleteAccess &&
                    readCoEPdoAssignmentCaLocked(
                        slave, assignment, direction, mappings) &&
                    hasExpectedBits()) {
                    return true;
                }
                mappings.resize(directionStart);

                if (readCoEPdoAssignmentLocked(
                        slave, assignment, direction, mappings) &&
                    hasExpectedBits()) {
                    return true;
                }
                mappings.resize(directionStart);
            }

            return readSiiPdoLocked(slave, direction, mappings) &&
                hasExpectedBits();
        };

        const bool rxComplete = readDirection(PdoDirection::Rx);
        const bool txComplete = readDirection(PdoDirection::Tx);
        const bool complete = rxComplete && txComplete;
        completeBySlave[static_cast<size_t>(slave)] = complete;
        allComplete = allComplete && complete;
    }
    return allComplete;
}

void EcatMasterBus::updatePdoIoMapOffsetsLocked(
    std::vector<ActivePdoEntry>& mappings) const
{
    const uintptr_t mapBegin = reinterpret_cast<uintptr_t>(ioMap_.data());
    for (ActivePdoEntry& entry : mappings) {
        if (!isValidSlaveAddress(entry.slave)) {
            continue;
        }

        const ec_slavet& slave = context_.slavelist[entry.slave];
        const uint8_t* base = entry.direction == PdoDirection::Rx
            ? slave.outputs
            : slave.inputs;
        const uint8_t startBit = entry.direction == PdoDirection::Rx
            ? slave.Ostartbit
            : slave.Istartbit;
        if (base == nullptr) {
            continue;
        }

        const uintptr_t address = reinterpret_cast<uintptr_t>(base);
        if (address >= mapBegin) {
            const uint64_t offset =
                static_cast<uint64_t>(address - mapBegin) * 8u +
                startBit + entry.bitOffset;
            entry.ioMapBitOffset = offset <= (std::numeric_limits<uint32_t>::max)()
                ? static_cast<uint32_t>(offset)
                : 0u;
        }
    }
}

std::vector<SlaveIdentitySnapshot>
EcatMasterBus::captureSlaveIdentitiesLocked(const bool readSerials)
{
    std::vector<SlaveIdentitySnapshot> identities;
    identities.reserve(static_cast<size_t>((std::max)(context_.slavecount, 0)));
    ecx_readstate(&context_);

    for (int slaveIndex = 1; slaveIndex <= context_.slavecount; ++slaveIndex) {
        const uint16_t slave = static_cast<uint16_t>(slaveIndex);
        const ec_slavet& item = context_.slavelist[slave];

        SlaveIdentitySnapshot identity;
        identity.position = slave;
        identity.name = item.name;
        identity.vendorId = item.eep_man;
        identity.productCode = item.eep_id;
        identity.revision = item.eep_rev;
        identity.eepromSerial = item.eep_ser;
        identity.outputBits = item.Obits;
        identity.inputBits = item.Ibits;
        identity.state = item.state;
        identity.alStatusCode = item.ALstatuscode;
        identity.alStatusText = alStatusText(item.ALstatuscode);

        if (readSerials &&
            (item.mbx_proto & ECT_MBXPROT_COE) != 0 &&
            baseState(item.state) >= EC_STATE_PRE_OP) {
            uint32_t rawSerial = 0;
            int size = sizeof(rawSerial);
            if (ecx_SDOread(&context_, slave, 0x1018, 4, FALSE,
                            &size, &rawSerial, EC_TIMEOUTRXM) > 0 &&
                size == static_cast<int>(sizeof(rawSerial))) {
                identity.serial = etohl(rawSerial);
            } else {
                checkForSdoErrors(slave, 0x1018);
            }
        }
        identities.push_back(std::move(identity));
    }
    return identities;
}

BusStateResult EcatMasterBus::currentStateResultLocked(
    const uint16_t requestedState,
    const std::string& error) const
{
    BusStateResult result;
    result.requestedState = requestedState;
    result.actualState = context_.slavecount > 0
        ? context_.slavelist[0].state
        : EC_STATE_NONE;
    result.workingCounter = wkc.load();
    result.error = error;
    result.success = error.empty() && context_.slavecount > 0;

    std::ostringstream failures;
    for (int slave = 1; slave <= context_.slavecount; ++slave) {
        const ec_slavet& item = context_.slavelist[slave];

        SlaveStateResult state;
        state.position = static_cast<uint16_t>(slave);
        state.requestedState = requestedState;
        state.actualState = item.state;
        state.alStatusCode = item.ALstatuscode;
        state.alStatusText = alStatusText(item.ALstatuscode);
        state.reached = stateReached(item.state, requestedState);
        result.slaves.push_back(std::move(state));

        if (!result.slaves.back().reached) {
            if (failures.tellp() > 0) {
                failures << "; ";
            }
            failures << "slave " << slave << " state 0x"
                     << std::hex << item.state << " AL 0x"
                     << item.ALstatuscode;
            result.success = false;
        }
    }

    if (result.error.empty() && !result.success) {
        result.error = "State transition incomplete: " + failures.str();
    }
    return result;
}

BusStateResult EcatMasterBus::writeStateLocked(const uint16_t requestedState)
{
    if (!socket_open_ || context_.slavecount <= 0) {
        return currentStateResultLocked(
            requestedState, "The EtherCAT master is not open.");
    }

    context_.slavelist[0].state = requestedState;
    const int writeWkc = ecx_writestate(&context_, 0);
    ecx_statecheck(&context_, 0, requestedState, EC_TIMEOUTSTATE);
    ecx_readstate(&context_);
    updateStateFlagsLocked();

    if (writeWkc <= 0) {
        return currentStateResultLocked(
            requestedState, "SOEM could not write the requested bus state.");
    }
    return currentStateResultLocked(requestedState);
}

void EcatMasterBus::updateStateFlagsLocked()
{
    const uint16_t state = context_.slavecount > 0
        ? baseState(context_.slavelist[0].state)
        : EC_STATE_NONE;
    init_ = state == EC_STATE_INIT;
    pre_op_ = state == EC_STATE_PRE_OP;
    operational_ = state == EC_STATE_OPERATIONAL;
}

bool EcatMasterBus::enableCyclicMailboxesLocked()
{
    bool enabled = false;
    if (!mapping_ready_) {
        return false;
    }

    for (int slave = 1; slave <= context_.slavecount; ++slave) {
        if (context_.slavelist[slave].mbx_l > 0 &&
            ecx_slavembxcyclic(&context_, static_cast<uint16_t>(slave)) > 0) {
            enabled = true;
        }
    }
    return enabled;
}

void EcatMasterBus::disableCyclicMailboxesLocked()
{
    for (int slave = 1; slave <= context_.slavecount; ++slave) {
        context_.slavelist[slave].mbxhandlerstate = ECT_MBXH_NONE;
    }
}

OnlineOdResult EcatMasterBus::readOnlineObjectDictionary(const uint16_t slave)
{
    OnlineOdResult result;
    std::lock_guard<std::mutex> mailboxLock(mailboxMutex_);

    {
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        if (!master_init_ || !isValidSlaveAddress(slave)) {
            result.error = "Invalid slave address or inactive EtherCAT bus.";
            return result;
        }
        const ec_slavet& item = context_.slavelist[slave];
        if ((item.mbx_proto & ECT_MBXPROT_COE) == 0 ||
            (item.CoEdetails & ECT_COEDET_SDOINFO) == 0) {
            result.error = "The slave does not advertise CoE SDO Info support.";
            return result;
        }
    }

    ec_ODlistt odList{};
    if (ecx_readODlist(&context_, slave, &odList) <= 0) {
        checkForSdoErrors(slave, 0);
        result.error = "The slave did not return an online object dictionary.";
        return result;
    }

    for (uint16_t item = 0; item < odList.Entries; ++item) {
        if (ecx_readODdescription(&context_, item, &odList) <= 0) {
            checkForSdoErrors(slave, odList.Index[item]);
            continue;
        }

        ec_OElistt oeList{};
        if (ecx_readOE(&context_, item, &odList, &oeList) <= 0) {
            checkForSdoErrors(slave, odList.Index[item]);
            continue;
        }

        for (uint16_t subindex = 0;
             subindex <= odList.MaxSub[item] && subindex < EC_MAXOELIST;
             ++subindex) {
            if (oeList.DataType[subindex] == 0 ||
                oeList.BitLength[subindex] == 0) {
                continue;
            }

            OnlineOdEntry entry;
            entry.slave = slave;
            entry.index = odList.Index[item];
            entry.subindex = static_cast<uint8_t>(subindex);
            entry.name = oeList.Name[subindex][0] != '\0'
                ? oeList.Name[subindex]
                : odList.Name[item];
            entry.dataType = oeList.DataType[subindex];
            entry.bitLength = oeList.BitLength[subindex];
            entry.objectAccess = oeList.ObjAccess[subindex];
            result.entries.push_back(std::move(entry));
        }
    }

    result.success = true;
    return result;
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
    std::lock_guard<std::recursive_mutex> lock(contextMutex_);
    bool matchingError = false;
    while (ecx_iserror(&context_)) {
        ec_errort error;
        if (ecx_poperror(&context_, &error)) {
            const std::string errorStr = getErrorString(error);
            std::cout << errorStr;
            matchingError = matchingError ||
                (error.Slave == slave && error.Index == index);
        }
    }
    return matchingError;
}

bool EcatMasterBus::sdoWrite(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf)
{
    std::lock_guard<std::mutex> mailboxLock(mailboxMutex_);
    {
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        if (!master_init_ || !isValidSlaveAddress(slave) ||
            size < 0 || (size > 0 && buf == nullptr)) {
            return false;
        }
    }

    const int writeWkc = ecx_SDOwrite(
        &context_, slave, index, subindex,
        static_cast<boolean>(completeAccess), size, buf, EC_TIMEOUTRXM);
    if (writeWkc <= 0) {
        std::cout << "Slave " << slave << ": Working counter too low ("
                  << writeWkc << ") for writing SDO (ID: 0x"
                  << std::setfill('0') << std::setw(4) << std::hex << index
                  << ", SID 0x" << std::setfill('0') << std::setw(2)
                  << static_cast<uint16_t>(subindex) << ").";
        checkForSdoErrors(slave, index);

        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        if (isValidSlaveAddress(slave)) {
            const uint16_t alCode = context_.slavelist[slave].ALstatuscode;
            std::cout << " AL status 0x" << std::setfill('0')
                      << std::setw(4) << std::hex << alCode << " "
                      << alStatusText(alCode);
        }
        return false;
    }
    return true;
}

bool EcatMasterBus::sdoRead(const uint16_t slave, const uint16_t index, const uint8_t subindex, const bool completeAccess, int size, void* buf)
{
    std::lock_guard<std::mutex> mailboxLock(mailboxMutex_);
    {
        std::lock_guard<std::recursive_mutex> contextLock(contextMutex_);
        if (!master_init_ || !isValidSlaveAddress(slave) ||
            size <= 0 || buf == nullptr) {
            return false;
        }
    }

    const int requestedSize = size;
    const int readWkc = ecx_SDOread(
        &context_, slave, index, subindex,
        static_cast<boolean>(completeAccess), &size, buf, EC_TIMEOUTRXM);
    if (readWkc <= 0) {
        std::cout << "Slave " << slave << ": Working counter too low ("
                  << readWkc << ") for reading SDO (ID: 0x"
                  << std::setfill('0') << std::setw(4) << std::hex << index
                  << ", SID 0x" << std::setfill('0') << std::setw(2)
                  << static_cast<uint16_t>(subindex) << ").";
        checkForSdoErrors(slave, index);
        return false;
    }
    if (size != requestedSize) {
        std::cout << "Slave " << slave << ": Size mismatch (expected "
                  << requestedSize << " bytes, read " << size
                  << " bytes) for reading SDO (ID: 0x"
                  << std::setfill('0') << std::setw(4) << std::hex << index
                  << ", SID 0x" << std::setfill('0') << std::setw(2)
                  << static_cast<uint16_t>(subindex) << ").";
        return false;
    }
    return true;
}

bool EcatMasterBus::applySDOConfigs(const std::vector<SDOConfig>& configs)
{
    for (const auto& cfg : configs) {
        bool success = false;
        if (cfg.type == SDOType::WRITE) {
            success = sdoWrite(
                cfg.slave, cfg.index, cfg.subindex, false,
                static_cast<int>(cfg.data.size()),
                const_cast<uint8_t*>(cfg.data.data()));
            std::printf("[SDO WRITE] slave=%d index=0x%X sub=0x%X %s\n",
                        cfg.slave, cfg.index, cfg.subindex,
                        success ? "OK" : "FAIL");
        } else {
            if (cfg.expected_size <= 0) {
                return false;
            }
            std::vector<uint8_t> buffer(
                static_cast<size_t>(cfg.expected_size));
            success = sdoRead(
                cfg.slave, cfg.index, cfg.subindex, false,
                cfg.expected_size, buffer.data());
            if (success) {
                std::printf("[SDO READ] slave=%d index=0x%X value=",
                            cfg.slave, cfg.index);
                for (const uint8_t byte : buffer) {
                    std::printf("%02X ", byte);
                }
                std::printf("\n");
            }
        }
        if (!success) {
            return false;
        }
    }
    return true;
}

} // namespace soem_interface

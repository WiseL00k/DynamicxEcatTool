#pragma once

#include "SOEM_interface/DcTestTypes.h"
#include "SOEM_interface/soem_interface_export.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace soem_interface::detail {

// The runtime owns the state machine; only transport and monotonic time are replaceable.
struct DcExchange { int wkc{0}; int64_t dcTimeNs{0}; bool dcTimeReceived{false}; };
SOEM_INTERFACE_EXPORT bool sync0ReadbackMatches(int activationWkc, uint8_t activation,
    int cycleWkc, uint32_t cycleNs, uint32_t requestedCycleNs);
SOEM_INTERFACE_EXPORT bool sync0ShutdownVerified(int activationWkc, uint8_t activation);
// Requires the already validated runtime period/shift range; preserves phase modulo cycle.
SOEM_INTERFACE_EXPORT int32_t sync0ShiftForSoem(uint32_t cycleNs, int32_t shiftNs);
SOEM_INTERFACE_EXPORT int64_t sync0PreheatNs(uint32_t cycleNs);
class DcWire {
public:
    virtual ~DcWire() = default;
    virtual bool discover(DcTestSnapshot&, std::string&) = 0;
    virtual bool configure(DcTestSnapshot&, std::string&) = 0;
    virtual bool requestState(uint16_t) = 0;
    virtual bool readStates(DcTestSnapshot&) = 0;
    virtual DcExchange exchange() = 0;
    virtual bool disableSync0(uint16_t) = 0;
    virtual void close() = 0;
};
class DcClock {
public:
    virtual ~DcClock() = default;
    virtual int64_t nowNs() = 0;
    virtual void waitUntil(int64_t deadlineNs, const std::atomic<bool>& stop) = 0;
    virtual void wake() = 0;
};

class SOEM_INTERFACE_EXPORT DcRuntime {
public:
    explicit DcRuntime(std::unique_ptr<DcWire>, std::unique_ptr<DcClock> = {});
    ~DcRuntime();
    DcStartResult start(const DcTestOptions&);
    void requestStop();
    void stop();
    DcTestSnapshot snapshot() const;
    void resetStatistics();
    bool active() const { return active_.load(); }
private:
    void run(DcTestOptions);
    void publish(const DcTestSnapshot&);
    void signalStarted(const DcStartResult&);
    std::unique_ptr<DcWire> wire_;
    std::unique_ptr<DcClock> clock_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> active_{false};
    std::atomic<uint64_t> resetGeneration_{0};
    mutable std::mutex snapshotMutex_;
    DcTestSnapshot snapshot_;
    std::mutex lifecycleMutex_;
    std::thread thread_;
    std::mutex startedMutex_;
    std::condition_variable startedCondition_;
    bool started_{false};
    DcStartResult startResult_;
};
} // namespace soem_interface::detail

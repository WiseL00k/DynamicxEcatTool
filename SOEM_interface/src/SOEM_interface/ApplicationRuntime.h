#pragma once

#include "DcRuntime.h"
#include "SOEM_interface/ApplicationTypes.h"

namespace soem_interface::detail {

// A busy mailbox defers AL polling/transitions without stopping PDO exchange.
enum class ApplicationPoll { Busy, Success, Failed };

class ApplicationWire {
public:
    virtual ~ApplicationWire() = default;
    virtual bool configure(const ApplicationRunOptions&, DcTestSnapshot&, std::string&) = 0;
    virtual ApplicationPoll requestState(uint16_t) = 0;
    virtual ApplicationPoll readStates(DcTestSnapshot&) = 0;
    virtual DcExchange exchange(const PrepareOutputs&, const ConsumeInputs&, bool applicationEnabled) = 0;
    virtual bool disableSync0(uint16_t) = 0;
    virtual void finish() = 0;
};

class SOEM_INTERFACE_EXPORT ApplicationRuntime {
public:
    explicit ApplicationRuntime(std::unique_ptr<ApplicationWire>, std::unique_ptr<DcClock> = {});
    ~ApplicationRuntime();
    DcStartResult start(const ApplicationRunOptions&, PrepareOutputs, ConsumeInputs);
    void requestStop();
    void stop();
    bool active() const { return active_.load(); }
    DcTestSnapshot snapshot() const;

private:
    void run(ApplicationRunOptions, PrepareOutputs, ConsumeInputs);
    void publish(const DcTestSnapshot&);
    void signalStarted(const DcStartResult&);
    std::unique_ptr<ApplicationWire> wire_;
    std::unique_ptr<DcClock> clock_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> active_{false};
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

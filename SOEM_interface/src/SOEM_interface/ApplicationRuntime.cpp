#include "ApplicationRuntime.h"
#include "CycleTiming.h"

#include <exception>

namespace soem_interface::detail {
namespace {
constexpr uint16_t kPreOp = 2, kSafeOp = 4, kOp = 8;

bool allAt(const DcTestSnapshot& s, uint16_t state) {
    return !s.slaves.empty() && std::all_of(s.slaves.begin(), s.slaves.end(),
        [state](const auto& slave) { return slave.state == state; });
}

void appendWarning(DcTestSnapshot& s, const std::string& warning) {
    if (!s.cleanupWarning.empty()) s.cleanupWarning += " ";
    s.cleanupWarning += warning;
}
}

ApplicationRuntime::ApplicationRuntime(std::unique_ptr<ApplicationWire> wire,
                                       std::unique_ptr<DcClock> clock)
    : wire_(std::move(wire)),
      clock_(clock ? std::move(clock) : makeSteadyDcClock()) {}

ApplicationRuntime::~ApplicationRuntime() { stop(); }

DcStartResult ApplicationRuntime::start(const ApplicationRunOptions& options,
                                        PrepareOutputs prepare, ConsumeInputs consume) {
    std::unique_lock<std::mutex> lifecycle(lifecycleMutex_);
    if (active_) return {false, false, "An application process-data run is already active."};
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(startedMutex_);
        started_ = false;
        startResult_ = {};
    }
    publish({});
    active_ = true;
    try {
        thread_ = std::thread(&ApplicationRuntime::run, this, options,
                              std::move(prepare), std::move(consume));
    } catch (const std::exception& ex) {
        active_ = false;
        DcTestSnapshot s;
        s.finished = true;
        s.error = ex.what();
        publish(s);
        return {false, false, s.error};
    }
    std::unique_lock<std::mutex> startedLock(startedMutex_);
    startedCondition_.wait(startedLock, [&] { return started_; });
    const auto result = startResult_;
    startedLock.unlock();
    if (!result.success && thread_.joinable()) thread_.join();
    return result;
}

void ApplicationRuntime::requestStop() {
    stopRequested_ = true;
    clock_->wake();
}

void ApplicationRuntime::stop() {
    requestStop();
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
    if (!active_) stopRequested_ = false;
}

DcTestSnapshot ApplicationRuntime::snapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

void ApplicationRuntime::publish(const DcTestSnapshot& s) {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = s;
}

void ApplicationRuntime::signalStarted(const DcStartResult& result) {
    std::lock_guard<std::mutex> lock(startedMutex_);
    if (!started_) {
        startResult_ = result;
        started_ = true;
        startedCondition_.notify_all();
    }
}

void ApplicationRuntime::run(ApplicationRunOptions options, PrepareOutputs prepare,
                              ConsumeInputs consume) {
    DcTestSnapshot s;
    std::vector<DcSlaveSnapshot> faultSlaves;
    const int64_t period = options.cycleUs >= 250 && options.cycleUs <= 1000000
        ? options.cycleUs * 1000 : 2000000;
    int64_t nextDeadline = clock_->nowNs(), lastSend = 0, lastExchange = 0;
    int64_t badWkcSince = -1, nextStatePoll = 0;
    bool configured = false, attempted = false, reachedOp = false;
    DcPhaseController phase;
    const auto publishCurrent = [&] {
        auto current = s;
        for (auto& slave : current.slaves) {
            const auto found = std::find_if(faultSlaves.begin(), faultSlaves.end(),
                [&](const auto& item) { return item.address == slave.address; });
            if (found != faultSlaves.end()) {
                slave.state = found->state;
                slave.alStatusCode = found->alStatusCode;
                slave.alStatusText = found->alStatusText;
            }
        }
        publish(current);
    };
    const auto advance = [&](bool cleanup) {
        advanceCycleDeadline(nextDeadline, clock_->nowNs(), period,
                             s.correctionNs, s.missedCycles);
        const std::atomic<bool> neverStop{false};
        clock_->waitUntil(nextDeadline, cleanup ? neverStop : stopRequested_);
    };
    const auto exchange = [&](bool applicationEnabled) {
        const auto sendTime = clock_->nowNs();
        lastExchange = sendTime;
        recordCycleTiming(s, sendTime, lastSend, period);
        const auto received = wire_->exchange(prepare, consume, applicationEnabled);
        ++s.cycleCount;
        s.wkc = received.wkc;
        const bool goodWkc = received.wkc == s.expectedWkc;
        if (!goodWkc) {
            ++s.wkcErrorCount;
            if (badWkcSince < 0) badWkcSince = sendTime;
        } else badWkcSince = -1;
        s.dcTimeNs = received.dcTimeNs;
        s.dcTimeValid = options.dcEnabled && goodWkc && received.dcTimeReceived && received.dcTimeNs > 0;
        phase.update(s, period, options.dcEnabled);
        publishCurrent();
    };
    const auto transition = [&](uint16_t target, bool cleanup, bool keepExchanging) {
        const auto deadline = clock_->nowNs() + std::max<int64_t>(2000000000, 5 * period);
        bool requested = false;
        do {
            if (!cleanup && stopRequested_) return false;
            if (keepExchanging) {
                if (lastExchange != 0 && nextDeadline <= lastExchange) advance(cleanup);
                if (!cleanup && stopRequested_) return false;
                exchange(false);
            }
            if (!requested) {
                const auto request = wire_->requestState(target);
                if (request == ApplicationPoll::Failed) return false;
                requested = request == ApplicationPoll::Success;
            }
            if (requested) {
                const auto read = wire_->readStates(s);
                if (read == ApplicationPoll::Failed) return false;
                if (read == ApplicationPoll::Success && allAt(s, target)) {
                    publishCurrent();
                    return true;
                }
            }
            advance(cleanup);
        } while (clock_->nowNs() < deadline);
        return false;
    };

    try {
        if (options.cycleUs < 250 || options.cycleUs > 1000000 ||
            options.shiftUs <= -options.cycleUs || options.shiftUs >= options.cycleUs) {
            s.error = "Cycle must be 250..1000000 us and shift must be strictly inside +/-cycle.";
        } else if (!stopRequested_) {
            s.cycleNs = static_cast<uint32_t>(period);
            s.shiftNs = static_cast<int32_t>(options.shiftUs * 1000);
            attempted = true;
            configured = wire_->configure(options, s, s.error);
            if (!configured && s.error.empty()) s.error = "Application process-data configuration failed.";
            if (configured && !stopRequested_) {
                s.configured = true;
                nextDeadline = clock_->nowNs();
                if (options.dcEnabled) {
                    const auto preheatEnd = nextDeadline + sync0PreheatNs(s.cycleNs);
                    while (!stopRequested_ && clock_->nowNs() < preheatEnd) {
                        exchange(false);
                        advance(false);
                    }
                }
                if (!stopRequested_) {
                    const bool ready = transition(kSafeOp, false, true);
                    if (!ready && !stopRequested_)
                        s.error = "Not all slaves reached SAFE-OP before the deadline.";
                }
                if (!stopRequested_ && s.error.empty()) {
                    const bool ready = transition(kOp, false, true);
                    if (!ready && !stopRequested_)
                        s.error = "Not all slaves reached OP before the deadline.";
                }
                if (!stopRequested_ && s.error.empty()) {
                    s.operational = true;
                    reachedOp = true;
                    badWkcSince = -1;
                    nextStatePoll = clock_->nowNs() + 100000000;
                    publishCurrent();
                    signalStarted({true, false, {}});
                    while (!stopRequested_ && s.error.empty()) {
                        advance(false);
                        if (stopRequested_) break;
                        exchange(true);
                        const auto now = clock_->nowNs();
                        if (now >= nextStatePoll || badWkcSince >= 0) {
                            const auto state = wire_->readStates(s);
                            if (state != ApplicationPoll::Busy) nextStatePoll = now + 100000000;
                            if (state == ApplicationPoll::Failed ||
                                (state == ApplicationPoll::Success && !allAt(s, kOp)))
                                s.error = "A slave left OP or its AL state could not be read.";
                        }
                        if (s.error.empty() && badWkcSince >= 0 &&
                            now - badWkcSince >= std::max<int64_t>(1000000000, 3 * period))
                            s.error = "The process-data WKC remained different from the expected value.";
                        publishCurrent();
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        s.error = std::string("Application runtime failed: ") + ex.what();
    } catch (...) {
        s.error = "Application runtime failed with an unknown exception.";
    }

    s.cancelled = stopRequested_ && s.error.empty();
    if (!s.error.empty()) faultSlaves = s.slaves;
    s.operational = false;
    if (attempted) {
        try {
            if (configured && !transition(kSafeOp, true, true))
                appendWarning(s, "SAFE-OP cleanup was not confirmed.");
        } catch (...) { appendWarning(s, "SAFE-OP cleanup failed."); }
        for (auto& slave : s.slaves) if (slave.sync0Requested) {
            slave.shutdownAttempted = true;
            try { slave.shutdownVerified = wire_->disableSync0(slave.address); }
            catch (...) { slave.shutdownVerified = false; }
            if (!slave.shutdownVerified)
                appendWarning(s, "SYNC0 shutdown was not verified for slave " +
                                 std::to_string(slave.address) + ".");
        }
        try {
            if (!s.slaves.empty() && !transition(kPreOp, true, false))
                appendWarning(s, "PRE-OP cleanup was not confirmed.");
        } catch (...) { appendWarning(s, "PRE-OP cleanup failed."); }
        try { wire_->finish(); }
        catch (...) { appendWarning(s, "Process-data cleanup failed."); }
    }
    s.finished = true;
    s.dcTimeValid = false;
    s.phaseValid = false;
    publishCurrent();
    active_ = false;
    if (!reachedOp) signalStarted({false, s.cancelled, s.error});
}

} // namespace soem_interface::detail

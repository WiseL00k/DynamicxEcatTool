#include "DcRuntime.h"
#include "CycleTiming.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace soem_interface::detail {
int64_t sync0PreheatNs(uint32_t cycleNs) {
    // SOEM/src/ec_dc.c defines SyncDelay as 100ms. After rounding to the
    // next cycle and adding the normalized shift, first trigger < now + 100ms + 2T.
    constexpr int64_t soemSyncDelayNs = 100000000;
    return std::max<int64_t>(200000000, 2 * int64_t(cycleNs) + soemSyncDelayNs);
}
int32_t sync0ShiftForSoem(uint32_t cycleNs, int32_t shiftNs) {
    // SOEM adds shift after rounding (now + 100ms) to a future whole cycle.
    // A negative shift can undo that guard or put the first trigger in the past.
    // The validated period is <= 1e9 ns, so this equivalent phase fits int32_t.
    return shiftNs < 0 ? static_cast<int32_t>(int64_t(shiftNs) + cycleNs) : shiftNs;
}
bool sync0ReadbackMatches(int activationWkc, uint8_t activation, int cycleWkc,
                         uint32_t cycleNs, uint32_t requestedCycleNs) {
    return activationWkc > 0 && cycleWkc > 0 && (activation & 0x07) == 0x03 &&
           cycleNs == requestedCycleNs;
}
bool sync0ShutdownVerified(int activationWkc, uint8_t activation) {
    return activationWkc > 0 && (activation & 0x07) == 0;
}
namespace {
constexpr uint16_t kInit = 1, kPreOp = 2, kSafeOp = 4, kOp = 8;
class SteadyDcClock final : public DcClock {
    std::mutex mutex_;
    std::condition_variable condition_;
public:
    int64_t nowNs() override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void waitUntil(int64_t deadline, const std::atomic<bool>& stop) override {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_until(lock, std::chrono::steady_clock::time_point(
            std::chrono::nanoseconds(deadline)), [&] { return stop.load(); });
    }
    void wake() override { condition_.notify_all(); }
};
bool allAt(const DcTestSnapshot& s, uint16_t state) {
    return !s.slaves.empty() && std::all_of(s.slaves.begin(), s.slaves.end(),
        [state](const auto& slave) { return slave.state == state; });
}
void clearStatistics(DcTestSnapshot& s) {
    s.cycleCount = s.wkcErrorCount = s.missedCycles = 0;
    s.actualCycleNs = s.minCycleNs = s.maxCycleNs = s.maxDeviationNs = 0;
}
void appendWarning(DcTestSnapshot& s, const std::string& warning) {
    if (!s.cleanupWarning.empty()) s.cleanupWarning += " ";
    s.cleanupWarning += warning;
}
}

std::unique_ptr<DcClock> makeSteadyDcClock() { return std::make_unique<SteadyDcClock>(); }

DcRuntime::DcRuntime(std::unique_ptr<DcWire> wire, std::unique_ptr<DcClock> clock)
    : wire_(std::move(wire)), clock_(clock ? std::move(clock) : makeSteadyDcClock()) {}
DcRuntime::~DcRuntime() { stop(); }

DcStartResult DcRuntime::start(const DcTestOptions& options) {
    std::unique_lock<std::mutex> lifecycle(lifecycleMutex_);
    if (active_) return {false, false, "DC 测试已在运行。"};
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(startedMutex_);
        started_ = false;
        startResult_ = {};
    }
    publish({});
    active_ = true;
    try { thread_ = std::thread(&DcRuntime::run, this, options); }
    catch (const std::exception& ex) {
        active_ = false;
        DcTestSnapshot s; s.finished = true;
        s.error = std::string("无法启动 DC 测试线程：") + ex.what(); publish(s);
        return {false, false, s.error};
    }
    std::unique_lock<std::mutex> startedLock(startedMutex_);
    startedCondition_.wait(startedLock, [&] { return started_; });
    const auto result = startResult_;
    startedLock.unlock();
    // Failure returns only after the transport and every requested SYNC0 are cleaned up.
    if (!result.success && thread_.joinable()) thread_.join();
    return result;
}
void DcRuntime::requestStop() { stopRequested_ = true; clock_->wake(); }
void DcRuntime::stop() {
    requestStop();
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
    // Consumed cancellation is cleared only after the run has fully joined, never by start().
    if (!active_) stopRequested_ = false;
}
DcTestSnapshot DcRuntime::snapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_); return snapshot_;
}
void DcRuntime::publish(const DcTestSnapshot& s) {
    std::lock_guard<std::mutex> lock(snapshotMutex_); snapshot_ = s;
}
void DcRuntime::resetStatistics() {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    ++resetGeneration_;
    clearStatistics(snapshot_);
}
void DcRuntime::signalStarted(const DcStartResult& result) {
    std::lock_guard<std::mutex> lock(startedMutex_);
    if (!started_) { startResult_ = result; started_ = true; startedCondition_.notify_all(); }
}

void DcRuntime::run(DcTestOptions options) {
    DcTestSnapshot s;
    std::vector<DcSlaveSnapshot> failureSlaves;
    bool transportAttempted = false, mapped = false, reachedOp = false;
    int64_t period = 0, nextDeadline = 0, lastSend = 0, lastExchange = 0, badWkcSince = -1;
    int64_t nextStatePoll = 0;
    DcPhaseController phaseController;
    uint64_t resetSeen = resetGeneration_;
    const auto cancelled = [&] { return stopRequested_.load(); };
    const auto syncReset = [&] {
        const auto generation = resetGeneration_.load();
        if (generation != resetSeen) {
            clearStatistics(s); lastSend = 0; resetSeen = generation;
        }
    };
    const auto publishCurrent = [&] {
        // Reset and publish share the snapshot lock, so an older local copy cannot undo reset.
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        syncReset(); snapshot_ = s;
        // Cleanup still reads real AL states to verify its transitions, while a failed run
        // keeps the fault scene in its published diagnostics. Shutdown fields remain current.
        for (auto& slave : snapshot_.slaves) {
            const auto fault = std::find_if(failureSlaves.begin(), failureSlaves.end(),
                [&](const auto& saved) { return saved.address == slave.address; });
            if (fault != failureSlaves.end()) {
                slave.state = fault->state;
                slave.alStatusCode = fault->alStatusCode;
                slave.alStatusText = fault->alStatusText;
            }
        }
    };
    const auto exchange = [&](bool checkOperational) {
        syncReset();
        const int64_t sendTime = clock_->nowNs();
        lastExchange = sendTime;
        recordCycleTiming(s, sendTime, lastSend, period);
        const auto received = wire_->exchange();
        ++s.cycleCount;
        s.wkc = received.wkc;
        const bool goodWkc = received.wkc == s.expectedWkc;
        const bool newWkcMismatch = !goodWkc && badWkcSince < 0;
        if (!goodWkc) {
            ++s.wkcErrorCount;
            if (badWkcSince < 0) badWkcSince = sendTime;
        } else badWkcSince = -1;
        s.dcTimeNs = received.dcTimeNs;
        s.dcTimeValid = goodWkc && received.dcTimeReceived && received.dcTimeNs > 0;
        phaseController.update(s, period);
        if (checkOperational) {
            if (clock_->nowNs() >= nextStatePoll || newWkcMismatch) {
                nextStatePoll = clock_->nowNs() + 100000000;
                if (!wire_->readStates(s) || !allAt(s, kOp))
                    s.error = "有从站退出 OP，或无法读取其 AL 状态。";
            }
            if (s.error.empty() && badWkcSince >= 0 &&
                clock_->nowNs() - badWkcSince >= std::max<int64_t>(1000000000, 3 * period)) {
                s.error = "过程数据的 WKC 持续与预期值不一致。";
            }
        }
        publishCurrent();
    };
    const auto advance = [&](bool duringCleanup) {
        advanceCycleDeadline(nextDeadline, clock_->nowNs(), period,
                             s.correctionNs, s.missedCycles);
        if (duringCleanup) {
            const std::atomic<bool> neverStop{false};
            clock_->waitUntil(nextDeadline, neverStop);
        } else clock_->waitUntil(nextDeadline, stopRequested_);
    };
    const auto transition = [&](uint16_t target, bool duringCleanup) {
        const auto deadline = clock_->nowNs() + std::max<int64_t>(2000000000, 5 * period);
        if (!wire_->requestState(target)) return false;
        do {
            if (!duringCleanup && cancelled()) return false;
            if (mapped) {
                // Adjacent AL transitions share the cyclic schedule; they cannot inject an extra frame.
                if (lastExchange != 0 && nextDeadline <= lastExchange) advance(duringCleanup);
                if (!duringCleanup && cancelled()) return false;
                exchange(false);
            }
            if (wire_->readStates(s) && allAt(s, target)) { publishCurrent(); return true; }
            advance(duringCleanup);
        } while (clock_->nowNs() < deadline);
        return false;
    };
    try {
        if (options.cycleUs < 250 || options.cycleUs > 1000000 ||
            options.shiftUs <= -options.cycleUs || options.shiftUs >= options.cycleUs) {
            s.error = "周期必须为 250–1000000 μs，相移必须严格处于负周期与正周期之间。";
        } else if (!cancelled()) {
            period = options.cycleUs * 1000;
            s.cycleNs = static_cast<uint32_t>(period);
            s.shiftNs = static_cast<int32_t>(options.shiftUs * 1000);
            nextDeadline = clock_->nowNs();
            transportAttempted = true;
            if (!wire_->discover(s, s.error)) {
                if (s.error.empty()) s.error = "EtherCAT 扫描失败。";
            } else if (!transition(kPreOp, false)) {
                if (!cancelled()) s.error = "部分从站未在规定时间内进入 PRE-OP。";
            } else if (!cancelled()) {
                if (!wire_->configure(s, s.error)) {
                    if (s.error.empty()) s.error = "DC 配置或 SYNC0 回读失败。";
                } else if (s.dcSlaveCount <= 0 || s.referenceSlave == 0) {
                    s.error = "未找到可用的 DC 参考从站。";
                } else if (s.expectedWkc <= 0) {
                    s.error = "总线没有已映射的 PDO 数据。";
                } else {
                    mapped = true; s.configured = true;
                    nextDeadline = clock_->nowNs();
                    const auto preheatEnd = nextDeadline + sync0PreheatNs(s.cycleNs);
                    while (!cancelled() && clock_->nowNs() < preheatEnd) { exchange(false); advance(false); }
                    if (!cancelled() && !transition(kSafeOp, false)) s.error = "部分从站未在规定时间内进入 SAFE-OP。";
                    if (!cancelled() && s.error.empty() && !transition(kOp, false)) s.error = "部分从站未在规定时间内进入 OP。";
                    if (!cancelled() && s.error.empty()) {
                        s.operational = true; reachedOp = true;
                        nextStatePoll = clock_->nowNs() + 100000000;
                        publishCurrent(); signalStarted({true, false, {}});
                        while (!cancelled() && s.error.empty()) { advance(false); if (!cancelled()) exchange(true); }
                    }
                }
            }
        }
    } catch (const std::exception& ex) { s.error = std::string("DC 运行失败：") + ex.what(); }
      catch (...) { s.error = "DC 运行失败，发生未知异常。"; }

    s.cancelled = cancelled() && s.error.empty();
    if (!s.error.empty()) failureSlaves = s.slaves;
    s.operational = false;
    // The worker cleans transport but never joins itself. The caller joins through stop/start.
    if (transportAttempted) {
        try {
            if (mapped && !transition(kSafeOp, true)) appendWarning(s, "退出清理时，未能确认已进入 SAFE-OP。");
        } catch (...) { appendWarning(s, "退出清理时，切换至 SAFE-OP 失败。"); }
        for (auto& slave : s.slaves) if (slave.sync0Requested) {
            slave.shutdownAttempted = true;
            try { slave.shutdownVerified = wire_->disableSync0(slave.address); }
            catch (...) { slave.shutdownVerified = false; }
            if (!slave.shutdownVerified) appendWarning(s, "未能验证 SYNC0 已关闭，从站 " + std::to_string(slave.address) + "。");
        }
        try {
            // No further PDO is needed once SYNC0 is off; return all slaves to INIT before close.
            mapped = false;
            if (!s.slaves.empty() && !transition(kInit, true)) appendWarning(s, "退出清理时，未能确认已进入 INIT。");
        } catch (...) { appendWarning(s, "退出清理时，切换至 INIT 失败。"); }
        try { wire_->close(); } catch (...) { appendWarning(s, "无法关闭 EtherCAT 网卡。"); }
    }
    s.operational = false; s.finished = true;
    s.dcTimeValid = false; s.phaseValid = false;
    publishCurrent();
    active_ = false;
    if (!reachedOp) signalStarted({false, s.cancelled, s.error});
}
} // namespace soem_interface::detail

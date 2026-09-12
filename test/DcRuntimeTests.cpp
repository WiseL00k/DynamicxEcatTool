#include <QtTest>
#include "SOEM_interface/DcRuntime.h"
#include "SOEM_interface/EcatMasterBus.h"
#include <limits>
#include <functional>
#include <future>

using namespace soem_interface;
using namespace soem_interface::detail;

class FakeClock final : public DcClock {
public:
    std::atomic<int64_t> time{1000000000};
    int64_t nowNs() override { return time.load(); }
    void waitUntil(int64_t deadline, const std::atomic<bool>&) override {
        if (deadline > time) time = deadline;
        std::this_thread::yield();
    }
    void wake() override {}
};
class FakeWire final : public DcWire {
public:
    FakeClock* clock{};
    DcRuntime* runtime{};
    int discovered{0}, exchanges{0}, closed{0};
    uint16_t state{1};
    bool noDc{false}, noPdo{false}, mismatch{false}, badReadback{false};
    bool shutdownFails{false};
    int mismatchWkc{1};
    int stopAfter{120}, dropOpAfter{0};
    uint16_t blockedState{0};
    uint16_t failureAlCode{0};
    int64_t opAt{0}, configuredAt{0};
    std::vector<uint16_t> requests, disabled;
    std::function<void(int)> onExchange;
    std::function<int64_t(int)> dcSample;
    std::vector<int64_t> sendTimes;
    std::vector<int64_t> opReadTimes;
    bool discover(DcTestSnapshot& s, std::string&) override {
        ++discovered;
        s.slaves = {{1, "non-DC", false}, {2, "DC-A", !noDc}, {3, "DC-B", !noDc}};
        for (auto& slave : s.slaves) slave.state = state;
        return true;
    }
    bool configure(DcTestSnapshot& s, std::string& error) override {
        configuredAt = clock->nowNs();
        if (noPdo) { error = "No mapped PDO."; return false; }
        s.expectedWkc = 3;
        s.referenceSlave = noDc ? 0 : 2;
        for (auto& slave : s.slaves) if (slave.hasDc) {
            ++s.dcSlaveCount;
            slave.sync0Requested = true;
            slave.sync0Verified = !badReadback;
            slave.cycleNs = s.cycleNs;
            slave.shiftNs = s.shiftNs;
            if (badReadback) { error = "SYNC0 readback failed."; return false; }
        }
        return true;
    }
    bool requestState(uint16_t target) override {
        requests.push_back(target); if (target != blockedState) state = target;
        if (target == 8) opAt = clock->nowNs();
        return true;
    }
    bool readStates(DcTestSnapshot& s) override {
        if (state == 8) opReadTimes.push_back(clock->nowNs());
        for (auto& slave : s.slaves) {
            slave.state = state;
            slave.alStatusCode = state == 4 && dropOpAfter && exchanges >= dropOpAfter ? failureAlCode : 0;
            slave.alStatusText = slave.alStatusCode ? "Sync manager watchdog" : "No error";
        }
        return true;
    }
    DcExchange exchange() override {
        ++exchanges;
        sendTimes.push_back(clock->nowNs());
        if (onExchange) onExchange(exchanges);
        if (dropOpAfter && state == 8 && exchanges >= dropOpAfter) state = 4;
        if (stopAfter && exchanges >= stopAfter) runtime->requestStop();
        return {mismatch ? mismatchWkc : 3, dcSample ? dcSample(exchanges) : clock->nowNs(), true};
    }
    bool disableSync0(uint16_t slave) override { disabled.push_back(slave); return !shutdownFails; }
    void close() override { ++closed; }
};
struct Fixture {
    FakeClock* clock;
    FakeWire* wire;
    DcRuntime runtime;
    Fixture() : Fixture(std::make_unique<FakeClock>(), std::make_unique<FakeWire>()) {}
    Fixture(std::unique_ptr<FakeClock> c, std::unique_ptr<FakeWire> w)
        : clock(c.get()), wire(w.get()), runtime(std::move(w), std::move(c)) {
        wire->clock = clock; wire->runtime = &runtime;
    }
    void finish() { while (runtime.active()) std::this_thread::yield(); runtime.stop(); }
};

class DcRuntimeTests : public QObject {
    Q_OBJECT
private slots:
    void rejectsOverflowAndBoundaryOptionsBeforeOpening() {
        for (const auto& o : {DcTestOptions{249, 0}, {1000001, 0}, {2000, 2000},
                              {2000, -2000}, {2000, (std::numeric_limits<int64_t>::min)()}}) {
            Fixture f;
            const auto result = f.runtime.start(o);
            QVERIFY(!result.success); QVERIFY(!result.error.empty());
            QCOMPARE(f.wire->discovered, 0);
            QVERIFY(f.runtime.snapshot().finished);
        }
    }
    void successfulStartUsesActualReferenceAndCleansEveryActivatedSlave() {
        Fixture f;
        const auto result = f.runtime.start({2000, -100});
        QVERIFY2(result.success, result.error.c_str());
        f.finish();
        const auto s = f.runtime.snapshot();
        QCOMPARE(s.referenceSlave, uint16_t(2)); QCOMPARE(s.dcSlaveCount, 2);
        QCOMPARE(s.cycleNs, uint32_t(2000000)); QCOMPARE(s.shiftNs, int32_t(-100000));
        QVERIFY(f.wire->opAt - f.wire->configuredAt >= 200000000);
        QCOMPARE(f.wire->disabled, (std::vector<uint16_t>{2, 3}));
        QCOMPARE(f.wire->closed, 1);
        QVERIFY(s.finished); QVERIFY(!s.operational);
        QVERIFY(s.slaves[1].shutdownVerified); QVERIFY(s.slaves[2].shutdownVerified);
        f.runtime.stop(); QCOMPARE(f.wire->closed, 1);
    }
    void cancellationBeforeStartIsStickyAndDoesNotOpen() {
        Fixture f; f.runtime.requestStop();
        const auto r = f.runtime.start({});
        QVERIFY(r.cancelled); QVERIFY(!r.success);
        QCOMPARE(f.wire->discovered, 0); QVERIFY(f.runtime.snapshot().finished);
    }
    void partialSyncFailureStillDisablesRequestedSlave() {
        Fixture f; f.wire->badReadback = true;
        const auto r = f.runtime.start({});
        QVERIFY(!r.success); QVERIFY(!r.error.empty());
        QCOMPARE(f.wire->disabled, (std::vector<uint16_t>{2}));
        QCOMPARE(f.wire->closed, 1); QVERIFY(f.runtime.snapshot().finished);
    }
    void noDcAndNoPdoAreRejectedBeforeOperational() {
        for (bool noDc : {false, true}) {
            Fixture f; f.wire->noDc = noDc; f.wire->noPdo = !noDc;
            const auto r = f.runtime.start({});
            QVERIFY(!r.success); QVERIFY(!r.error.empty());
            QCOMPARE(f.wire->opAt, int64_t(0)); QCOMPARE(f.wire->closed, 1);
        }
    }
    void lostOperationalStateFinishesAndPreservesCleanupFailure() {
        Fixture f; f.wire->stopAfter = 0; f.wire->dropOpAfter = 110;
        f.wire->shutdownFails = true;
        QVERIFY(f.runtime.start({}).success);
        f.finish();
        const auto s = f.runtime.snapshot();
        QVERIFY(s.finished); QVERIFY(!s.error.empty()); QVERIFY(!s.cleanupWarning.empty());
        QVERIFY(s.slaves[1].shutdownAttempted); QVERIFY(!s.slaves[1].shutdownVerified);
        QCOMPARE(f.wire->disabled.size(), size_t(2));
    }
    void wkcMismatchUsesElapsedDurationNotArbitraryCount() {
        Fixture f; f.wire->stopAfter = 0;
        f.wire->onExchange = [&](int count) { if (count > 105) f.wire->mismatch = true; };
        QVERIFY(f.runtime.start({}).success);
        f.finish();
        const auto s = f.runtime.snapshot();
        QVERIFY(!s.error.empty()); QVERIFY(s.wkcErrorCount >= 500);
        QVERIFY(f.clock->nowNs() - f.wire->opAt >= 1000000000);
    }
    void longCyclePreheatScalesWithPeriod() {
        Fixture f; f.wire->stopAfter = 8;
        QVERIFY(f.runtime.start({1000000, 999999}).success);
        f.finish();
        QVERIFY(f.wire->opAt - f.wire->configuredAt >= 2000000000);
        QCOMPARE(f.runtime.snapshot().shiftNs, int32_t(999999000));
    }
    void stateTransitionsAndOverrunsNeverSendCatchupBursts() {
        Fixture f;
        f.wire->onExchange = [&](int count) { if (count == 110) f.clock->time += 17000000; };
        QVERIFY(f.runtime.start({}).success); f.finish();
        for (size_t i = 1; i < f.wire->sendTimes.size(); ++i)
            QVERIFY2(f.wire->sendTimes[i] - f.wire->sendTimes[i - 1] >= 1900000,
                     "A state transition or overrun sent a catch-up burst");
        QVERIFY(f.runtime.snapshot().missedCycles >= 8);
    }
    void stateTimeoutKeepsPdoFlowingForTheScaledDeadline() {
        Fixture f; f.wire->stopAfter = 0; f.wire->blockedState = 8;
        const auto r = f.runtime.start({1000000, 0});
        QVERIFY(!r.success); QVERIFY(!r.error.empty());
        QVERIFY(f.clock->nowNs() - f.wire->opAt >= 5000000000);
        QVERIFY(f.wire->exchanges >= 7); QCOMPARE(f.wire->closed, 1);
    }
    void phaseUsesMidpointAndShiftAndRejectsRepeatedBackwardSamples() {
        Fixture f;
        std::vector<DcTestSnapshot> observations;
        f.wire->dcSample = [](int count) -> int64_t {
            if (count == 106) return 1211600000; // Same as count 105.
            if (count == 107) return 1209600000; // Older than high-water mark.
            return 1001600000 + int64_t(count) * 2000000;
        };
        f.wire->onExchange = [&](int count) {
            if (count == 2 || count == 107 || count == 108 || count == 109)
                observations.push_back(f.runtime.snapshot());
        };
        QVERIFY(f.runtime.start({2000, 500}).success); f.finish();
        QCOMPARE(observations.size(), size_t(4));
        QCOMPARE(observations[0].phaseErrorNs, int64_t(100000));
        QCOMPARE(observations[0].correctionNs, int64_t(-1002));
        QVERIFY(!observations[1].phaseValid); QCOMPARE(observations[1].correctionNs, int64_t(0));
        QVERIFY(!observations[2].phaseValid); QCOMPARE(observations[2].correctionNs, int64_t(0));
        QVERIFY(observations[3].phaseValid);
    }
    void statisticsResetDoesNotResetIntegralOrDcHistory() {
        Fixture f;
        DcTestSnapshot before, after;
        f.wire->dcSample = [](int count) { return 1001600000 + int64_t(count) * 2000000; };
        f.wire->onExchange = [&](int count) {
            if (count == 110) { before = f.runtime.snapshot(); f.runtime.resetStatistics(); }
            if (count == 112) after = f.runtime.snapshot();
        };
        QVERIFY(f.runtime.start({2000, 500}).success); f.finish();
        QVERIFY(after.cycleCount < before.cycleCount);
        QCOMPARE(after.phaseErrorNs, before.phaseErrorNs);
        QCOMPARE(after.correctionNs, before.correctionNs - 4);
        QVERIFY(after.phaseValid);
    }
    void snapshotsAndStopRequestRemainResponsiveDuringBlockedReceive() {
        Fixture f; f.wire->stopAfter = 0;
        std::promise<void> entered, release;
        auto unblock = release.get_future().share();
        f.wire->onExchange = [&](int count) { if (count == 110) { entered.set_value(); unblock.wait(); } };
        QVERIFY(f.runtime.start({}).success);
        const auto enteredStatus = entered.get_future().wait_for(std::chrono::seconds(2));
        if (enteredStatus != std::future_status::ready) { release.set_value(); f.runtime.stop(); QFAIL("Receive did not block"); }
        const auto begin = std::chrono::steady_clock::now();
        const auto snapshot = f.runtime.snapshot(); f.runtime.resetStatistics(); f.runtime.requestStop();
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        release.set_value(); f.runtime.stop();
        QVERIFY(snapshot.operational);
        QVERIFY(elapsed < std::chrono::milliseconds(100));
        QVERIFY(f.runtime.snapshot().finished); QCOMPARE(f.wire->closed, 1);
    }
    void integralSaturatesAndInvalidSamplesSuppressCorrection() {
        Fixture f; f.wire->stopAfter = 15000;
        DcTestSnapshot clamped, invalid;
        f.wire->dcSample = [](int count) -> int64_t {
            return count == 14001 ? 0 : 1001800000 + int64_t(count) * 2000000;
        };
        f.wire->onExchange = [&](int count) {
            if (count == 14001) clamped = f.runtime.snapshot();
            if (count == 14002) invalid = f.runtime.snapshot();
        };
        QVERIFY(f.runtime.start({}).success); f.finish();
        QCOMPARE(clamped.phaseErrorNs, int64_t(800000));
        QCOMPARE(clamped.correctionNs, int64_t(-28000));
        QVERIFY(!invalid.dcTimeValid); QVERIFY(!invalid.phaseValid);
        QCOMPARE(invalid.correctionNs, int64_t(0));
    }
    void transientWkcMismatchRecoversAndOvercountAlsoCountsAsError() {
        Fixture f; f.wire->stopAfter = 300; f.wire->mismatchWkc = 5;
        f.wire->onExchange = [&](int count) { f.wire->mismatch = count >= 110 && count < 200; };
        QVERIFY(f.runtime.start({}).success); f.finish();
        const auto s = f.runtime.snapshot();
        QVERIFY(s.error.empty()); QCOMPARE(s.wkcErrorCount, uint64_t(90));
    }
    void cancellationInterruptsRealLongPeriodWait() {
        FakeClock sourceClock;
        auto wire = std::make_unique<FakeWire>();
        auto* transport = wire.get(); transport->clock = &sourceClock; transport->stopAfter = 0;
        DcRuntime runtime(std::move(wire)); transport->runtime = &runtime;
        std::promise<void> firstExchange;
        transport->onExchange = [&](int count) { if (count == 1) firstExchange.set_value(); };
        auto starting = std::async(std::launch::async, [&] { return runtime.start({1000000, 0}); });
        const auto started = firstExchange.get_future().wait_for(std::chrono::seconds(2));
        const auto begin = std::chrono::steady_clock::now(); runtime.requestStop();
        const auto result = starting.get(); runtime.stop();
        QVERIFY(started == std::future_status::ready); QVERIFY(result.cancelled);
        QVERIFY(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(500));
        QCOMPARE(transport->closed, 1);
    }
    void publicBusRetainsInvalidAndCancelledResultsWithoutOpeningANic() {
        auto bus = std::make_unique<EcatMasterBus>();
        const auto invalid = bus->startDcTest({249, 0});
        QVERIFY(!invalid.success); QVERIFY(!invalid.error.empty());
        QVERIFY(bus->dcTestSnapshot().finished); QVERIFY(!bus->isMasterInitialized());
        bus->requestDcStop();
        const auto cancelled = bus->startDcTest({});
        QVERIFY(cancelled.cancelled); bus->stopDcTest();
        QVERIFY(bus->dcTestSnapshot().cancelled); bus->stop();
        const auto emptyNic = bus->startDcTest({});
        QVERIFY(!emptyNic.success); QVERIFY(!emptyNic.cancelled); QVERIFY(!emptyNic.error.empty());
    }
    void alStatusPollingIsThrottledAndCommunicationAnomalyTriggersImmediateRead() {
        Fixture f; f.wire->stopAfter = 300;
        QVERIFY(f.runtime.start({}).success); f.finish();
        QVERIFY(f.wire->opReadTimes.size() >= 3);
        QVERIFY(f.wire->opReadTimes.size() <= 6);
        for (size_t i = 1; i < f.wire->opReadTimes.size(); ++i)
            QVERIFY(f.wire->opReadTimes[i] - f.wire->opReadTimes[i - 1] >= 100000000);
        Fixture anomaly; anomaly.wire->stopAfter = 300;
        int64_t mismatchTime = 0;
        anomaly.wire->onExchange = [&](int count) {
            if (count == 110) { mismatchTime = anomaly.clock->nowNs(); anomaly.wire->mismatch = true; }
        };
        QVERIFY(anomaly.runtime.start({}).success); anomaly.finish();
        QVERIFY(std::find(anomaly.wire->opReadTimes.begin(), anomaly.wire->opReadTimes.end(), mismatchTime)
                != anomaly.wire->opReadTimes.end());
    }
    void finishedSnapshotDoesNotAdvertiseLiveClockOrPhaseValidity() {
        Fixture f; QVERIFY(f.runtime.start({}).success); f.finish();
        const auto s = f.runtime.snapshot();
        QVERIFY(s.finished); QVERIFY(!s.dcTimeValid); QVERIFY(!s.phaseValid);
        QVERIFY(s.dcTimeNs > 0); QVERIFY(s.cycleCount > 0);
    }
    void productionReadbackValidationRequiresBothResponsesAndMatchingRegisters() {
        QVERIFY(sync0ReadbackMatches(1, 3, 1, 2000000, 2000000));
        QVERIFY(!sync0ReadbackMatches(0, 3, 1, 2000000, 2000000));
        QVERIFY(!sync0ReadbackMatches(1, 3, 0, 2000000, 2000000));
        QVERIFY(!sync0ReadbackMatches(-1, 3, 1, 2000000, 2000000));
        QVERIFY(!sync0ReadbackMatches(1, 1, 1, 2000000, 2000000));
        QVERIFY(!sync0ReadbackMatches(1, 7, 1, 2000000, 2000000));
        QVERIFY(!sync0ReadbackMatches(1, 3, 1, 1000000, 2000000));
        QVERIFY(sync0ShutdownVerified(1, 0));
        QVERIFY(!sync0ShutdownVerified(0, 0));
        QVERIFY(!sync0ShutdownVerified(1, 2));
        QVERIFY(!sync0ShutdownVerified(1, 4));
    }
    void failedRunRetainsAlFaultWhileCleanupStillUpdatesShutdownResults() {
        Fixture f; f.wire->stopAfter = 0; f.wire->dropOpAfter = 110;
        f.wire->failureAlCode = 0x001b;
        QVERIFY(f.runtime.start({}).success); f.finish();
        const auto s = f.runtime.snapshot();
        QVERIFY(s.finished); QVERIFY(!s.error.empty());
        QCOMPARE(s.slaves[1].state, uint16_t(4));
        QCOMPARE(s.slaves[1].alStatusCode, uint16_t(0x001b));
        QCOMPARE(s.slaves[1].alStatusText, std::string("Sync manager watchdog"));
        QVERIFY(s.slaves[1].shutdownAttempted); QVERIFY(s.slaves[1].shutdownVerified);
        QVERIFY(s.slaves[2].shutdownAttempted); QVERIFY(s.slaves[2].shutdownVerified);
        QCOMPARE(f.wire->state, uint16_t(1)); QCOMPARE(f.wire->closed, 1);
        QVERIFY(!s.dcTimeValid); QVERIFY(!s.phaseValid);
        Fixture normal;
        QVERIFY(normal.runtime.start({}).success); normal.finish();
        const auto stopped = normal.runtime.snapshot();
        QVERIFY(stopped.error.empty());
        QCOMPARE(stopped.slaves[1].state, uint16_t(1));
        QCOMPARE(stopped.slaves[1].alStatusCode, uint16_t(0));
    }
    void soemShiftNormalizationPreservesPhaseAtBothPeriodBoundaries() {
        struct Case { uint32_t cycle; int32_t requested; int32_t wire; };
        const Case cases[] = {
            {250000, -249000, 1000}, {250000, -1000, 249000},
            {250000, 0, 0}, {250000, 249000, 249000},
            {1000000000, -999999000, 1000}, {1000000000, -1000, 999999000},
            {1000000000, 0, 0}, {1000000000, 999999000, 999999000}
        };
        for (const auto& c : cases) {
            const auto wire = sync0ShiftForSoem(c.cycle, c.requested);
            QCOMPARE(wire, c.wire);
            QVERIFY(wire >= 0); QVERIFY(uint32_t(wire) < c.cycle);
            QCOMPARE((int64_t(wire) - c.requested) % c.cycle, int64_t(0));
        }
    }
    void normalizedShiftKeepsVendoredSoemFirstTriggerBeyondItsGuard() {
        // Characterize the actual ecx_dcsync0 formula from SOEM/src/ec_dc.c.
        // The production adapter passes this helper's result to that function.
        constexpr int64_t syncDelay = 100000000;
        for (uint32_t cycle : {250000u, 1000000000u}) {
            const int32_t edge = static_cast<int32_t>(cycle) - 1000;
            for (int32_t shift : {-edge, int32_t(-1000), int32_t(0), edge}) {
                for (int64_t phase : {int64_t(0), int64_t(cycle / 2), int64_t(cycle - 1)}) {
                    const int64_t now = 1000000000000 + phase;
                    const auto normalized = sync0ShiftForSoem(cycle, shift);
                    const int64_t first = ((now + syncDelay) / cycle) * cycle + cycle + normalized;
                    QVERIFY2(first > now + syncDelay, "SYNC0 must not be placed in the past or inside SOEM's guard");
                    QCOMPARE((first - shift) % cycle, int64_t(0));
                }
            }
        }
    }
    void preheatCoversVendoredFirstPulseUpperBoundAtBoundaryPeriods() {
        constexpr int64_t syncDelay = 100000000; // SOEM/src/ec_dc.c: SyncDelay.
        for (uint32_t cycle : {250000u, 1000000000u}) {
            const auto preheat = sync0PreheatNs(cycle);
            QVERIFY(preheat >= 200000000); QVERIFY(preheat >= 2 * int64_t(cycle));
            for (int32_t shift : {int32_t(0), int32_t(cycle - 1000), -int32_t(cycle - 1000)}) {
                for (int64_t phase : {int64_t(0), int64_t(cycle) * 91 / 100, int64_t(cycle - 1)}) {
                    const int64_t now = 1000000000000 + phase;
                    const auto normalized = sync0ShiftForSoem(cycle, shift);
                    const int64_t first = ((now + syncDelay) / cycle) * cycle + cycle + normalized;
                    QVERIFY2(first < now + preheat,
                             "SAFEOP/OP preheat must cover the first SYNC0 pulse, including the SOEM guard");
                }
            }
        }
        QCOMPARE(sync0PreheatNs(250000), int64_t(200000000));
        QCOMPARE(sync0PreheatNs(1000000000), int64_t(2100000000));
    }
};
QTEST_GUILESS_MAIN(DcRuntimeTests)
#include "DcRuntimeTests.moc"

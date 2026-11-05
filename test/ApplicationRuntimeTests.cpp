#include <QtTest>
#include "SOEM_interface/ApplicationRuntime.h"
#include <atomic>
#include <stdexcept>

using namespace soem_interface;
using namespace soem_interface::detail;

namespace {
class Clock final : public DcClock {
public:
    std::atomic<int64_t> now{1000000000};
    int64_t nowNs() override { return now.load(); }
    void waitUntil(int64_t deadline, const std::atomic<bool>&) override {
        if (deadline > now) now = deadline;
        std::this_thread::yield();
    }
    void wake() override {}
};
class Wire final : public ApplicationWire {
public:
    Clock* clock{};
    ApplicationRuntime* runtime{};
    uint16_t state{2};
    int configured{}, finished{}, disabled{}, appCycles{}, zeroCycles{}, busyReads{};
    int stopAfter{600};
    uint16_t cancelOnState{};
    bool busyInOp{}, badWkc{}, shutdownFailure{}, dropOp{};
    int64_t opTime{}, configuredTime{};
    std::vector<uint16_t> states;
    bool configure(const ApplicationRunOptions& options, DcTestSnapshot& s, std::string&) override {
        ++configured; state = 2; appCycles = 0;
        configuredTime = clock->nowNs();
        s.expectedWkc = 3;
        s.slaves = {{1, "ECAT2CANFD", true}};
        s.slaves[0].state = state;
        s.slaves[0].sync0Requested = options.dcEnabled;
        s.slaves[0].sync0Verified = options.dcEnabled;
        s.slaves[0].cycleNs = static_cast<uint32_t>(options.cycleUs * 1000);
        s.referenceSlave = options.dcEnabled ? 1 : 0;
        s.dcSlaveCount = options.dcEnabled ? 1 : 0;
        return true;
    }
    ApplicationPoll requestState(uint16_t target) override {
        states.push_back(target); state = target;
        if (target == cancelOnState) {
            cancelOnState = 0;
            runtime->requestStop();
            return ApplicationPoll::Busy;
        }
        if (target == 8) opTime = clock->nowNs();
        return ApplicationPoll::Success;
    }
    ApplicationPoll readStates(DcTestSnapshot& s) override {
        if (busyInOp && state == 8 && appCycles > 0) {
            ++busyReads;
            return ApplicationPoll::Busy;
        }
        s.slaves[0].state = dropOp && appCycles > 50 && state == 8 ? 4 : state;
        return ApplicationPoll::Success;
    }
    DcExchange exchange(const PrepareOutputs& prepare, const ConsumeInputs& consume, bool enabled) override {
        if (enabled) {
            ++appCycles;
            std::vector<SlaveProcessImage> images{{1, std::vector<uint8_t>(185), std::vector<uint8_t>(185)}};
            if (prepare) prepare(images);
            if (consume) consume({static_cast<uint64_t>(appCycles), clock->nowNs(), badWkc ? 0 : 3, 3,
                                  clock->nowNs(), std::move(images)});
            if (appCycles == stopAfter) runtime->requestStop();
        } else ++zeroCycles;
        return {badWkc && enabled ? 0 : 3, clock->nowNs(), true};
    }
    bool disableSync0(uint16_t) override { ++disabled; return !shutdownFailure; }
    void finish() override { ++finished; }
};
struct Fixture {
    Clock* clock;
    Wire* wire;
    ApplicationRuntime runtime;
    Fixture() : Fixture(std::make_unique<Clock>(), std::make_unique<Wire>()) {}
    Fixture(std::unique_ptr<Clock> c, std::unique_ptr<Wire> w)
        : clock(c.get()), wire(w.get()), runtime(std::move(w), std::move(c)) {
        wire->clock = clock; wire->runtime = &runtime;
    }
    void awaitFinished() { while (runtime.active()) std::this_thread::yield(); runtime.stop(); }
};
}

class ApplicationRuntimeTests : public QObject {
    Q_OBJECT
private slots:
    void ordinaryRunReturnsPreOpAndCanRestart() {
        Fixture f;
        std::atomic<int> inputs{0}, outputs{0};
        QVERIFY(f.runtime.start({}, [&](auto&) { ++outputs; }, [&](const auto&) { ++inputs; }).success);
        f.awaitFinished();
        QCOMPARE(inputs.load(), 600);
        QCOMPARE(outputs.load(), 600);
        QCOMPARE(f.wire->state, uint16_t(2));
        QVERIFY(f.wire->zeroCycles >= 2);
        QVERIFY(f.runtime.snapshot().error.empty());
        QVERIFY(!f.runtime.snapshot().dcTimeValid);
        QVERIFY(f.runtime.start({}, {}, {}).success);
        f.awaitFinished();
        QCOMPARE(f.wire->configured, 2);
        QCOMPARE(f.wire->finished, 2);
    }
    void busySdoDoesNotStopPdo() {
        Fixture f;
        f.wire->busyInOp = true;
        QVERIFY(f.runtime.start({}, {}, {}).success);
        f.awaitFinished();
        QCOMPARE(f.wire->appCycles, 600);
        QVERIFY(f.wire->busyReads > 0);
        QVERIFY(f.runtime.snapshot().error.empty());
    }
    void dcPreheatsAndDisablesSync0() {
        Fixture f;
        QVERIFY(f.runtime.start({2000, true, -500}, {}, {}).success);
        f.awaitFinished();
        QVERIFY(f.wire->opTime - f.wire->configuredTime >= sync0PreheatNs(2000000));
        QCOMPARE(f.wire->disabled, 1);
        QVERIFY(f.runtime.snapshot().cleanupWarning.empty());
    }
    void callbackFailureClearsOutputsAndLeavesOp() {
        Fixture f;
        QVERIFY(f.runtime.start({}, [](auto&) { throw std::runtime_error("callback failure"); }, {}).success);
        f.awaitFinished();
        QVERIFY(QString::fromStdString(f.runtime.snapshot().error).contains("callback failure"));
        QCOMPARE(f.wire->state, uint16_t(2));
        QCOMPARE(f.wire->finished, 1);
        QVERIFY(f.wire->zeroCycles >= 3);
    }
    void persistentWkcFaultRetainsReason() {
        Fixture f;
        f.wire->badWkc = true;
        f.wire->stopAfter = 1000;
        QVERIFY(f.runtime.start({}, {}, {}).success);
        f.awaitFinished();
        QVERIFY(QString::fromStdString(f.runtime.snapshot().error).contains("WKC"));
        QVERIFY(f.wire->appCycles >= 500 && f.wire->appCycles < 1000);
        QCOMPARE(f.wire->state, uint16_t(2));
    }
    void shutdownFailureIsNotReportedAsVerified() {
        Fixture f;
        f.wire->shutdownFailure = true;
        QVERIFY(f.runtime.start({2000, true, 0}, {}, {}).success);
        f.awaitFinished();
        QVERIFY(!f.runtime.snapshot().cleanupWarning.empty());
        QVERIFY(!f.runtime.snapshot().slaves[0].shutdownVerified);
    }
    void invalidOptionsDoNotTouchBus() {
        Fixture f;
        QVERIFY(!f.runtime.start({249, false, 0}, {}, {}).success);
        QCOMPARE(f.wire->configured, 0);
    }
    void cancelledStartCanBeRetried() {
        Fixture f;
        f.runtime.requestStop();
        QVERIFY(f.runtime.start({}, {}, {}).cancelled);
        f.runtime.stop();
        QVERIFY(f.runtime.start({}, {}, {}).success);
        f.awaitFinished();
    }
    void cancelledStateTransitionCanBeRetried_data() {
        QTest::addColumn<int>("state");
        QTest::newRow("SAFE-OP transition") << 4;
        QTest::newRow("OP transition") << 8;
    }
    void cancelledStateTransitionCanBeRetried() {
        QFETCH(int, state);
        Fixture f;
        f.wire->cancelOnState = static_cast<uint16_t>(state);
        std::atomic<int> outputs{0}, inputs{0};
        const auto result = f.runtime.start({}, [&](auto&) { ++outputs; },
                                             [&](const auto&) { ++inputs; });
        QVERIFY(!result.success);
        QVERIFY(result.cancelled);
        QVERIFY(result.error.empty());
        f.awaitFinished();
        QCOMPARE(outputs.load(), 0);
        QCOMPARE(inputs.load(), 0);
        QCOMPARE(f.wire->state, uint16_t(2));
        QCOMPARE(f.wire->finished, 1);
        QVERIFY(f.runtime.snapshot().cancelled);
        QVERIFY(f.runtime.snapshot().error.empty());
        QVERIFY(f.runtime.snapshot().cleanupWarning.empty());
        QVERIFY(f.runtime.start({}, {}, {}).success);
        f.awaitFinished();
        QCOMPARE(f.wire->finished, 2);
    }
};
QTEST_GUILESS_MAIN(ApplicationRuntimeTests)
#include "ApplicationRuntimeTests.moc"

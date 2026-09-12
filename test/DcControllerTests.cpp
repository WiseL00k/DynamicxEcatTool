#include "Backend/Dc/DcTestController.h"
#include "Backend/Ethercat/BusSessionCoordinator.h"
#include "Backend/Ethercat/EthercatBackend.h"

#include <QSignalSpy>
#include <QtTest>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace {

struct SessionState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool allowStart{false};
    bool stopRequested{false};
    bool failStart{false};
    bool throwOnStart{false};
    bool throwOnStop{false};
    bool throwOnReset{false};
    std::atomic_bool inStart{false};
    std::atomic_int starts{0};
    std::atomic_int stops{0};
    std::atomic_int resets{0};
    std::atomic_bool stopOnGuiThread{false};
    std::string nicName;
    soem_interface::DcTestOptions options;
    soem_interface::DcTestSnapshot snapshot;
};

class FakeDcTestSession final : public Backend::DcTestSession
{
public:
    explicit FakeDcTestSession(std::shared_ptr<SessionState> state)
        : state_(std::move(state))
    {}

    soem_interface::DcStartResult start(
        const std::string& nicName,
        const soem_interface::DcTestOptions& options) override
    {
        ++state_->starts;
        if (state_->throwOnStart) {
            throw std::runtime_error("start adapter exception");
        }
        state_->inStart = true;
        std::unique_lock lock(state_->mutex);
        state_->nicName = nicName;
        state_->options = options;
        state_->changed.wait(lock, [this] {
            return state_->allowStart || state_->stopRequested;
        });
        state_->inStart = false;
        if (state_->stopRequested) {
            state_->snapshot.cancelled = true;
            state_->snapshot.finished = true;
            return {false, true, {}};
        }
        if (state_->failStart) {
            state_->snapshot.error = "Controlled startup failure";
            state_->snapshot.finished = true;
            return {false, false, state_->snapshot.error};
        }
        state_->snapshot.configured = true;
        state_->snapshot.operational = true;
        return {true, false, {}};
    }

    void requestStop() override
    {
        std::lock_guard lock(state_->mutex);
        state_->stopRequested = true;
        state_->changed.notify_all();
    }

    void stop() override
    {
        ++state_->stops;
        state_->stopOnGuiThread = QThread::currentThread() == qApp->thread();
        if (state_->throwOnStop) {
            throw std::runtime_error("stop adapter exception");
        }
        std::lock_guard lock(state_->mutex);
        state_->snapshot.operational = false;
        state_->snapshot.finished = true;
        state_->stopRequested = false;
    }

    soem_interface::DcTestSnapshot snapshot() const override
    {
        std::lock_guard lock(state_->mutex);
        return state_->snapshot;
    }

    void resetStatistics() override
    {
        ++state_->resets;
        if (state_->throwOnReset) {
            throw std::runtime_error("reset adapter exception");
        }
    }

private:
    std::shared_ptr<SessionState> state_;
};

std::unique_ptr<Backend::DcTestSession> fakeSession(
    const std::shared_ptr<SessionState>& state)
{
    return std::make_unique<FakeDcTestSession>(state);
}

void allowStart(const std::shared_ptr<SessionState>& state)
{
    std::lock_guard lock(state->mutex);
    state->allowStart = true;
    state->changed.notify_all();
}

void populateSnapshot(SessionState& state)
{
    auto& snapshot = state.snapshot;
    snapshot.configured = true;
    snapshot.operational = true;
    snapshot.dcTimeValid = true;
    snapshot.phaseValid = true;
    snapshot.referenceSlave = 2;
    snapshot.dcSlaveCount = 1;
    snapshot.cycleNs = 2'000'000;
    snapshot.shiftNs = -100'000;
    snapshot.wkc = 3;
    snapshot.expectedWkc = 3;
    snapshot.dcTimeNs = 9'007'199'254'740'993LL;
    snapshot.cycleCount = 9'007'199'254'740'995ULL;
    snapshot.wkcErrorCount = 9'007'199'254'740'997ULL;
    snapshot.missedCycles = 9'007'199'254'740'999ULL;
    snapshot.actualCycleNs = 2'001'000;
    snapshot.minCycleNs = 1'998'000;
    snapshot.maxCycleNs = 2'004'000;
    snapshot.maxDeviationNs = 4'000;
    snapshot.phaseErrorNs = -12'000;
    snapshot.correctionNs = 4'000;
    snapshot.slaves = {{2, "DC fixture", true, true, true, false, false,
                        2'000'000, -100'000, 140, 8, 0, "No error"}};
}

} // namespace

class DcControllerTests : public QObject
{
    Q_OBJECT

private slots:
    void backendExposesControllerAndKeepsDcLogsOnTheDedicatedRoute()
    {
        Backend::EthercatBackend backend;
        QVERIFY(backend.dcTest());
        const int propertyIndex = backend.metaObject()->indexOfProperty("dcTest");
        QVERIFY(propertyIndex >= 0);
        QVERIFY(backend.metaObject()->property(propertyIndex).isConstant());

        QSignalSpy dcLogs(&backend, &Backend::EthercatBackend::dcLogAppended);
        QSignalSpy unrelatedLogs(&backend, &Backend::EthercatBackend::logAppend);
        QSignalSpy errors(&backend, &Backend::EthercatBackend::soemErrorOccurred);
        QSignalSpy dcErrors(backend.dcTest(), &Backend::DcTestController::errorOccurred);
        backend.dcTest()->start(2'000, 0);

        QCOMPARE(dcLogs.size(), 1);
        QCOMPARE(unrelatedLogs.size(), 0);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(dcErrors.size(), 1);
    }

    void reservesBeforeStartAndCancellationReleasesOnlyAfterCleanup()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        Backend::DcTestController controller(coordinator, fakeSession(state));
        controller.setNicName("fixture0");

        controller.start(4'000, -100);
        QCOMPARE(controller.state(), QStringLiteral("starting"));
        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::DcTest);
        QCOMPARE(coordinator.modeName(), QStringLiteral("DC测试"));
        QTRY_VERIFY(state->inStart.load());

        controller.stop();
        QCOMPARE(controller.state(), QStringLiteral("stopping"));
        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::DcTest);
        QTRY_COMPARE(controller.state(), QStringLiteral("idle"));

        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::Idle);
        QCOMPARE(state->starts.load(), 1);
        QCOMPARE(state->stops.load(), 1);
        QVERIFY(!state->stopOnGuiThread.load());
        QVERIFY(controller.hasResults());
        QVERIFY(controller.historical());
    }

    void publishesExactMapsWithoutLosingWideIntegersAndRetainsHistory()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        populateSnapshot(*state);
        allowStart(state);
        Backend::DcTestController controller(coordinator, fakeSession(state));
        controller.setNicName("fixture0");

        controller.start(2'000, -100);
        QTRY_COMPARE(controller.state(), QStringLiteral("running"));

        const QVariantMap diagnostics = controller.diagnostics();
        QStringList keys = diagnostics.keys();
        keys.sort();
        QStringList expectedKeys{
            "actualCycleNs", "cancelled", "cleanupWarning", "configured",
            "correctionNs", "cycleCount", "cycleNs", "dcSlaveCount",
            "dcTimeNs", "dcTimeValid", "error", "expectedWkc", "finished",
            "maxCycleNs", "maxDeviationNs", "minCycleNs", "missedCycles",
            "operational", "phaseErrorNs", "phaseValid", "referenceSlave",
            "shiftNs", "wkc", "wkcErrorCount"};
        expectedKeys.sort();
        QCOMPARE(keys, expectedKeys);
        QCOMPARE(diagnostics.value("dcTimeNs").toString(),
                 QStringLiteral("9007199254740993"));
        QCOMPARE(diagnostics.value("cycleCount").toString(),
                 QStringLiteral("9007199254740995"));
        QCOMPARE(diagnostics.value("wkcErrorCount").toString(),
                 QStringLiteral("9007199254740997"));
        QCOMPARE(diagnostics.value("missedCycles").toString(),
                 QStringLiteral("9007199254740999"));
        QCOMPARE(diagnostics.value("phaseErrorNs").toLongLong(), -12'000LL);

        const QVariantList slaves = controller.slavesModel();
        QCOMPARE(slaves.size(), 1);
        QStringList slaveKeys = slaves.first().toMap().keys();
        slaveKeys.sort();
        QStringList expectedSlaveKeys{
            "address", "alStatusCode", "alStatusText", "cycleNs", "hasDc",
            "name", "propagationDelayNs", "shiftNs", "shutdownAttempted",
            "shutdownVerified", "state", "sync0Requested", "sync0Verified"};
        expectedSlaveKeys.sort();
        QCOMPARE(slaveKeys, expectedSlaveKeys);

        controller.stop();
        QTRY_COMPARE(controller.state(), QStringLiteral("idle"));
        QVERIFY(controller.historical());
        QCOMPARE(controller.diagnostics().value("dcTimeNs").toString(),
                 QStringLiteral("9007199254740993"));
    }

    void runtimeFailureTriggersCleanupAndKeepsTheFailure()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        populateSnapshot(*state);
        allowStart(state);
        Backend::DcTestController controller(coordinator, fakeSession(state));
        controller.setNicName("fixture0");
        QSignalSpy errors(&controller, &Backend::DcTestController::errorOccurred);

        controller.start(2'000, 0);
        QTRY_COMPARE(controller.state(), QStringLiteral("running"));
        {
            std::lock_guard lock(state->mutex);
            state->snapshot.operational = false;
            state->snapshot.finished = true;
            state->snapshot.error = "Cyclic exchange failed";
        }

        QTRY_COMPARE_WITH_TIMEOUT(controller.state(), QStringLiteral("failed"), 2'000);
        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::Idle);
        QCOMPARE(state->stops.load(), 1);
        QCOMPARE(controller.diagnostics().value("error").toString(),
                 QStringLiteral("Cyclic exchange failed"));
        QCOMPARE(errors.size(), 1);
    }

    void acceptedRestartDoesNotExposePreviousRunAsLiveData()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        populateSnapshot(*state);
        allowStart(state);
        Backend::DcTestController controller(coordinator, fakeSession(state));
        controller.setNicName("fixture0");

        controller.start(2'000, 0);
        QTRY_COMPARE(controller.state(), QStringLiteral("running"));
        controller.stop();
        QTRY_COMPARE(controller.state(), QStringLiteral("idle"));
        QVERIFY(!controller.diagnostics().isEmpty());

        {
            std::lock_guard lock(state->mutex);
            state->allowStart = false;
            state->snapshot = {};
        }
        controller.start(4'000, 100);

        QCOMPARE(controller.state(), QStringLiteral("starting"));
        QVERIFY(controller.diagnostics().isEmpty());
        QVERIFY(controller.slavesModel().isEmpty());
        QVERIFY(!controller.hasResults());
        controller.stop();
        QTRY_COMPARE(controller.state(), QStringLiteral("idle"));
    }

    void sessionExceptionsFailSafelyAndReleaseTheReservation()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        state->throwOnStart = true;
        Backend::DcTestController controller(coordinator, fakeSession(state));
        controller.setNicName("fixture0");
        QSignalSpy errors(&controller, &Backend::DcTestController::errorOccurred);

        controller.start(2'000, 0);

        QTRY_COMPARE(controller.state(), QStringLiteral("failed"));
        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::Idle);
        QVERIFY(!errors.isEmpty());
        QVERIFY(errors.last().first().toString().contains("start adapter exception"));
    }

    void stopAndResetExceptionsAreReportedWithoutLeakingTheSession()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        populateSnapshot(*state);
        allowStart(state);
        Backend::DcTestController controller(coordinator, fakeSession(state));
        controller.setNicName("fixture0");
        QSignalSpy errors(&controller, &Backend::DcTestController::errorOccurred);
        controller.start(2'000, 0);
        QTRY_COMPARE(controller.state(), QStringLiteral("running"));

        state->throwOnReset = true;
        controller.resetStatistics();
        QVERIFY(!errors.isEmpty());
        QVERIFY(errors.last().first().toString().contains("reset adapter exception"));

        state->throwOnStop = true;
        controller.stop();
        QTRY_COMPARE(controller.state(), QStringLiteral("failed"));
        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::Idle);
        QVERIFY(errors.last().first().toString().contains("stop adapter exception"));
    }

    void resetStatisticsUsesTheActiveSession()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        populateSnapshot(*state);
        allowStart(state);
        Backend::DcTestController controller(coordinator, fakeSession(state));
        controller.setNicName("fixture0");

        controller.start(2'000, 0);
        QTRY_COMPARE(controller.state(), QStringLiteral("running"));
        controller.resetStatistics();
        QCOMPARE(state->resets.load(), 1);
        controller.stop();
        QTRY_COMPARE(controller.state(), QStringLiteral("idle"));
    }

    void destructionCancelsPendingStartAndWaitsForStop()
    {
        Backend::BusSessionCoordinator coordinator;
        auto state = std::make_shared<SessionState>();
        auto controller = std::make_unique<Backend::DcTestController>(
            coordinator, fakeSession(state));
        controller->setNicName("fixture0");
        controller->start(2'000, 0);
        QTRY_VERIFY(state->inStart.load());

        controller.reset();

        QVERIFY(!state->inStart.load());
        QCOMPARE(state->stops.load(), 1);
        QVERIFY(!state->stopOnGuiThread.load());
        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::Idle);
    }

    void idleControllerDestructionDoesNotReleaseAnotherDcOwner()
    {
        Backend::BusSessionCoordinator coordinator;
        QString error;
        QVERIFY(coordinator.tryAcquire(Backend::BusSessionCoordinator::Mode::DcTest,
                                       error));
        auto state = std::make_shared<SessionState>();
        {
            Backend::DcTestController controller(coordinator, fakeSession(state));
        }

        QCOMPARE(coordinator.mode(), Backend::BusSessionCoordinator::Mode::DcTest);
        QCOMPARE(state->stops.load(), 0);
        QVERIFY(coordinator.release(Backend::BusSessionCoordinator::Mode::DcTest));
    }
};

QTEST_GUILESS_MAIN(DcControllerTests)
#include "DcControllerTests.moc"

#include "Backend/Dc/DcTestController.h"

#include "Backend/Ethercat/BusSessionCoordinator.h"
#include "Backend/Ethercat/EthercatMasterController.h"

#include <QMetaObject>
#include <QPointer>

#include <exception>
#include <utility>

namespace Backend {
namespace {

constexpr int kRefreshIntervalMs = 200;

QString fromUtf8(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

class MasterDcTestSession final : public DcTestSession
{
public:
    explicit MasterDcTestSession(EthercatMasterController& masterController)
        : masterController_(masterController)
    {}

    soem_interface::DcStartResult start(
        const std::string& nicName,
        const soem_interface::DcTestOptions& options) override
    {
        return masterController_.startDcTest(nicName, options);
    }

    void requestStop() override
    {
        masterController_.requestDcStop();
    }

    void stop() override
    {
        masterController_.stopDcTest();
    }

    soem_interface::DcTestSnapshot snapshot() const override
    {
        return masterController_.dcTestSnapshot();
    }

    void resetStatistics() override
    {
        masterController_.resetDcStatistics();
    }

private:
    EthercatMasterController& masterController_;
};

} // namespace

DcTestController::DcTestController(BusSessionCoordinator& coordinator,
                                   EthercatMasterController& masterController,
                                   QObject* parent)
    : DcTestController(coordinator,
                       std::make_unique<MasterDcTestSession>(masterController),
                       parent)
{}

DcTestController::DcTestController(BusSessionCoordinator& coordinator,
                                   std::unique_ptr<DcTestSession> session,
                                   QObject* parent)
    : QObject(parent)
    , coordinator_(coordinator)
    , session_(std::move(session))
{
    Q_ASSERT(session_);
    workerPool_.setMaxThreadCount(1);
    refreshTimer_.setInterval(kRefreshIntervalMs);
    refreshTimer_.setTimerType(Qt::CoarseTimer);
    connect(&refreshTimer_, &QTimer::timeout,
            this, &DcTestController::refreshSnapshot);
}

DcTestController::~DcTestController()
{
    destroying_ = true;
    refreshTimer_.stop();

    if (ownsSession_) {
        try {
            session_->requestStop();
        } catch (...) {
            // Destruction still runs the blocking idempotent cleanup below.
        }
        DcTestSession* const session = session_.get();
        workerPool_.start([session] {
            try {
                session->stop();
            } catch (...) {
                // There is no receiver left to report teardown errors to.
            }
        });
    }
    workerPool_.waitForDone();
    if (ownsSession_) {
        coordinator_.release(BusSessionCoordinator::Mode::DcTest);
        ownsSession_ = false;
    }
}

QString DcTestController::state() const
{
    switch (state_) {
    case State::Idle: return QStringLiteral("idle");
    case State::Starting: return QStringLiteral("starting");
    case State::Running: return QStringLiteral("running");
    case State::Stopping: return QStringLiteral("stopping");
    case State::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

QString DcTestController::statusText() const
{
    switch (state_) {
    case State::Idle: return QStringLiteral("未运行");
    case State::Starting: return QStringLiteral("正在启动");
    case State::Running: return QStringLiteral("正在运行");
    case State::Stopping: return QStringLiteral("正在停止");
    case State::Failed: return QStringLiteral("测试失败");
    }
    return QStringLiteral("未知状态");
}

bool DcTestController::active() const
{
    return isActiveState(state_);
}

bool DcTestController::busy() const
{
    return isBusyState(state_);
}

bool DcTestController::hasResults() const
{
    return hasResults_;
}

bool DcTestController::historical() const
{
    return hasResults_ && !active();
}

QVariantMap DcTestController::diagnostics() const
{
    return diagnostics_;
}

QVariantList DcTestController::slavesModel() const
{
    return slaves_;
}

void DcTestController::setNicName(const std::string& nicName)
{
    if (!active()) {
        nicName_ = nicName;
    }
}

void DcTestController::start(qint64 cycleUs, qint64 shiftUs)
{
    if (active()) {
        const QString message = QStringLiteral("DC 测试正在运行");
        appendLog(message);
        emit errorOccurred(message);
        return;
    }
    if (nicName_.empty()) {
        const QString message = QStringLiteral("请先选择网卡");
        appendLog(message);
        emit errorOccurred(message);
        return;
    }

    QString sessionError;
    if (!coordinator_.tryAcquire(BusSessionCoordinator::Mode::DcTest,
                                 sessionError)) {
        appendLog(sessionError);
        emit errorOccurred(sessionError);
        return;
    }
    ownsSession_ = true;

    const quint64 generation = ++generation_;
    cleanupScheduled_ = false;
    const bool wasHistorical = historical();
    diagnostics_.clear();
    slaves_.clear();
    hasResults_ = false;
    state_ = State::Starting;

    const std::string nicName = nicName_;
    const soem_interface::DcTestOptions options{
        static_cast<int64_t>(cycleUs), static_cast<int64_t>(shiftUs)};
    DcTestSession* const session = session_.get();
    const QPointer<DcTestController> self(this);
    workerPool_.start([self, session, generation, nicName, options] {
        soem_interface::DcStartResult result;
        soem_interface::DcTestSnapshot snapshot;
        try {
            result = session->start(nicName, options);
            try {
                snapshot = session->snapshot();
            } catch (const std::exception& exception) {
                result = {false, false,
                          std::string("DC session snapshot failed: ")
                              + exception.what()};
            } catch (...) {
                result = {false, false, "DC session snapshot failed."};
            }
        } catch (const std::exception& exception) {
            result = {false, false,
                      std::string("DC session start failed: ")
                          + exception.what()};
            try {
                snapshot = session->snapshot();
            } catch (...) {
            }
        } catch (...) {
            result = {false, false, "DC session start failed."};
            try {
                snapshot = session->snapshot();
            } catch (...) {
            }
        }
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, generation, result = std::move(result),
             snapshot = std::move(snapshot)]() mutable {
                if (self) {
                    self->completeStart(generation, std::move(result),
                                        std::move(snapshot));
                }
            },
            Qt::QueuedConnection);
    });

    // Publish the starting state only after its worker has been queued. A direct
    // state observer may call stop() immediately, and its cleanup must follow
    // (never precede) the pending start operation in the serialized pool.
    emit stateChanged();
    emit resultsChanged();
    if (wasHistorical != historical()) {
        emit historicalChanged();
    }
    appendLog(QStringLiteral("开始 DC 测试：周期 %1 μs，相移 %2 μs")
                  .arg(cycleUs)
                  .arg(shiftUs));
}

void DcTestController::stop()
{
    if (!active() || state_ == State::Stopping) {
        return;
    }

    refreshTimer_.stop();
    const quint64 generation = ++generation_;
    State finalState = State::Idle;
    QString stopError;
    try {
        session_->requestStop();
    } catch (const std::exception& exception) {
        finalState = State::Failed;
        stopError = QStringLiteral("DC 停止请求失败：%1")
                        .arg(QString::fromUtf8(exception.what()));
    } catch (...) {
        finalState = State::Failed;
        stopError = QStringLiteral("DC 停止请求失败");
    }
    setState(State::Stopping);
    appendLog(QStringLiteral("正在停止 DC 测试"));
    scheduleCleanup(generation, finalState, stopError);
}

void DcTestController::resetStatistics()
{
    if (!hasResults_ || busy()) {
        return;
    }

    if (active()) {
        try {
            session_->resetStatistics();
            refreshSnapshot();
        } catch (const std::exception& exception) {
            const QString message = QStringLiteral("DC 统计重置失败：%1")
                                        .arg(QString::fromUtf8(exception.what()));
            appendLog(message);
            emit errorOccurred(message);
            return;
        } catch (...) {
            const QString message = QStringLiteral("DC 统计重置失败");
            appendLog(message);
            emit errorOccurred(message);
            return;
        }
    } else {
        diagnostics_[QStringLiteral("cycleCount")] = QStringLiteral("0");
        diagnostics_[QStringLiteral("wkcErrorCount")] = QStringLiteral("0");
        diagnostics_[QStringLiteral("missedCycles")] = QStringLiteral("0");
        diagnostics_[QStringLiteral("actualCycleNs")] = qlonglong{0};
        diagnostics_[QStringLiteral("minCycleNs")] = qlonglong{0};
        diagnostics_[QStringLiteral("maxCycleNs")] = qlonglong{0};
        diagnostics_[QStringLiteral("maxDeviationNs")] = qlonglong{0};
        emit resultsChanged();
    }
    appendLog(QStringLiteral("DC 统计数据已重置"));
}

void DcTestController::setState(State state)
{
    if (state_ == state) {
        return;
    }
    const bool wasHistorical = historical();
    state_ = state;
    emit stateChanged();
    if (wasHistorical != historical()) {
        emit historicalChanged();
    }
}

void DcTestController::appendLog(const QString& line)
{
    if (!line.isEmpty()) {
        emit logAppended(line);
    }
}

void DcTestController::refreshSnapshot()
{
    if (state_ != State::Running) {
        return;
    }

    soem_interface::DcTestSnapshot snapshot;
    try {
        snapshot = session_->snapshot();
    } catch (const std::exception& exception) {
        refreshTimer_.stop();
        const quint64 generation = ++generation_;
        const QString message = QStringLiteral("DC 快照读取失败：%1")
                                    .arg(QString::fromUtf8(exception.what()));
        setState(State::Stopping);
        scheduleCleanup(generation, State::Failed, message);
        return;
    } catch (...) {
        refreshTimer_.stop();
        const quint64 generation = ++generation_;
        setState(State::Stopping);
        scheduleCleanup(generation, State::Failed,
                        QStringLiteral("DC 快照读取失败"));
        return;
    }
    publishSnapshot(snapshot);
    if (!snapshot.finished) {
        return;
    }

    refreshTimer_.stop();
    const quint64 generation = ++generation_;
    const QString errorMessage = fromUtf8(snapshot.error);
    setState(State::Stopping);
    scheduleCleanup(generation,
                    errorMessage.isEmpty() ? State::Idle : State::Failed,
                    errorMessage);
}

void DcTestController::publishSnapshot(
    const soem_interface::DcTestSnapshot& snapshot)
{
    const bool wasHistorical = historical();
    hasResults_ = true;
    diagnostics_ = diagnosticsMap(snapshot);
    slaves_ = slaveMaps(snapshot);
    emit resultsChanged();
    if (wasHistorical != historical()) {
        emit historicalChanged();
    }
}

void DcTestController::completeStart(
    quint64 generation,
    soem_interface::DcStartResult result,
    soem_interface::DcTestSnapshot snapshot)
{
    if (destroying_ || generation != generation_) {
        return;
    }

    publishSnapshot(snapshot);
    if (!result.success) {
        QString message = fromUtf8(result.error);
        if (message.isEmpty()) {
            message = fromUtf8(snapshot.error);
        }
        if (message.isEmpty() && !result.cancelled) {
            message = QStringLiteral("DC 测试启动失败");
        }
        setState(State::Stopping);
        scheduleCleanup(generation,
                        result.cancelled ? State::Idle : State::Failed,
                        message);
        return;
    }

    if (snapshot.finished) {
        const QString message = fromUtf8(snapshot.error);
        setState(State::Stopping);
        scheduleCleanup(generation,
                        message.isEmpty() ? State::Idle : State::Failed,
                        message);
        return;
    }

    setState(State::Running);
    emit sessionConnected(static_cast<int>(snapshot.slaves.size()));
    appendLog(QStringLiteral("DC 测试已进入 OP，共 %1 个从站")
                  .arg(snapshot.slaves.size()));
    refreshTimer_.start();
}

void DcTestController::scheduleCleanup(quint64 generation,
                                       State finalState,
                                       QString errorMessage)
{
    if (cleanupScheduled_) {
        return;
    }
    cleanupScheduled_ = true;

    DcTestSession* const session = session_.get();
    const QPointer<DcTestController> self(this);
    workerPool_.start(
        [self, session, generation, finalState,
         errorMessage = std::move(errorMessage)]() mutable {
            soem_interface::DcTestSnapshot snapshot;
            try {
                session->stop();
            } catch (const std::exception& exception) {
                finalState = State::Failed;
                errorMessage = QStringLiteral("DC 清理失败：%1")
                                   .arg(QString::fromUtf8(exception.what()));
            } catch (...) {
                finalState = State::Failed;
                errorMessage = QStringLiteral("DC 清理失败");
            }
            try {
                snapshot = session->snapshot();
            } catch (const std::exception& exception) {
                finalState = State::Failed;
                if (errorMessage.isEmpty()) {
                    errorMessage = QStringLiteral("DC 最终快照读取失败：%1")
                                       .arg(QString::fromUtf8(exception.what()));
                }
            } catch (...) {
                finalState = State::Failed;
                if (errorMessage.isEmpty()) {
                    errorMessage = QStringLiteral("DC 最终快照读取失败");
                }
            }
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, generation, finalState,
                 errorMessage = std::move(errorMessage),
                 snapshot = std::move(snapshot)]() mutable {
                    if (self) {
                        self->completeCleanup(generation, finalState,
                                              std::move(errorMessage),
                                              std::move(snapshot));
                    }
                },
                Qt::QueuedConnection);
        });
}

void DcTestController::completeCleanup(
    quint64 generation,
    State finalState,
    QString errorMessage,
    soem_interface::DcTestSnapshot snapshot)
{
    if (destroying_ || generation != generation_) {
        return;
    }

    cleanupScheduled_ = false;
    publishSnapshot(snapshot);
    if (ownsSession_) {
        emit sessionReleased();
        coordinator_.release(BusSessionCoordinator::Mode::DcTest);
        ownsSession_ = false;
    }
    setState(finalState);

    const QString cleanupWarning = fromUtf8(snapshot.cleanupWarning);
    if (!cleanupWarning.isEmpty()) {
        appendLog(QStringLiteral("DC 清理警告：%1").arg(cleanupWarning));
    }
    if (finalState == State::Failed) {
        if (errorMessage.isEmpty()) {
            errorMessage = fromUtf8(snapshot.error);
        }
        if (errorMessage.isEmpty()) {
            errorMessage = QStringLiteral("DC 测试失败");
        }
        appendLog(errorMessage);
        emit errorOccurred(errorMessage);
    } else {
        appendLog(snapshot.cancelled
                      ? QStringLiteral("DC 测试已取消")
                      : QStringLiteral("DC 测试已停止"));
    }
}

bool DcTestController::isActiveState(State state)
{
    return state == State::Starting
        || state == State::Running
        || state == State::Stopping;
}

bool DcTestController::isBusyState(State state)
{
    return state == State::Starting || state == State::Stopping;
}

QVariantMap DcTestController::diagnosticsMap(
    const soem_interface::DcTestSnapshot& snapshot)
{
    return {
        {QStringLiteral("configured"), snapshot.configured},
        {QStringLiteral("operational"), snapshot.operational},
        {QStringLiteral("dcTimeValid"), snapshot.dcTimeValid},
        {QStringLiteral("phaseValid"), snapshot.phaseValid},
        {QStringLiteral("finished"), snapshot.finished},
        {QStringLiteral("cancelled"), snapshot.cancelled},
        {QStringLiteral("referenceSlave"), snapshot.referenceSlave},
        {QStringLiteral("dcSlaveCount"), snapshot.dcSlaveCount},
        {QStringLiteral("cycleNs"), snapshot.cycleNs},
        {QStringLiteral("shiftNs"), snapshot.shiftNs},
        {QStringLiteral("wkc"), snapshot.wkc},
        {QStringLiteral("expectedWkc"), snapshot.expectedWkc},
        {QStringLiteral("dcTimeNs"), QString::number(snapshot.dcTimeNs)},
        {QStringLiteral("cycleCount"), QString::number(snapshot.cycleCount)},
        {QStringLiteral("wkcErrorCount"), QString::number(snapshot.wkcErrorCount)},
        {QStringLiteral("missedCycles"), QString::number(snapshot.missedCycles)},
        {QStringLiteral("actualCycleNs"), qlonglong(snapshot.actualCycleNs)},
        {QStringLiteral("minCycleNs"), qlonglong(snapshot.minCycleNs)},
        {QStringLiteral("maxCycleNs"), qlonglong(snapshot.maxCycleNs)},
        {QStringLiteral("maxDeviationNs"), qlonglong(snapshot.maxDeviationNs)},
        {QStringLiteral("phaseErrorNs"), qlonglong(snapshot.phaseErrorNs)},
        {QStringLiteral("correctionNs"), qlonglong(snapshot.correctionNs)},
        {QStringLiteral("error"), fromUtf8(snapshot.error)},
        {QStringLiteral("cleanupWarning"), fromUtf8(snapshot.cleanupWarning)},
    };
}

QVariantList DcTestController::slaveMaps(
    const soem_interface::DcTestSnapshot& snapshot)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(snapshot.slaves.size()));
    for (const soem_interface::DcSlaveSnapshot& slave : snapshot.slaves) {
        result.push_back(QVariantMap{
            {QStringLiteral("address"), slave.address},
            {QStringLiteral("name"), fromUtf8(slave.name)},
            {QStringLiteral("hasDc"), slave.hasDc},
            {QStringLiteral("sync0Requested"), slave.sync0Requested},
            {QStringLiteral("sync0Verified"), slave.sync0Verified},
            {QStringLiteral("shutdownAttempted"), slave.shutdownAttempted},
            {QStringLiteral("shutdownVerified"), slave.shutdownVerified},
            {QStringLiteral("cycleNs"), slave.cycleNs},
            {QStringLiteral("shiftNs"), slave.shiftNs},
            {QStringLiteral("propagationDelayNs"), slave.propagationDelayNs},
            {QStringLiteral("state"), slave.state},
            {QStringLiteral("alStatusCode"), slave.alStatusCode},
            {QStringLiteral("alStatusText"), fromUtf8(slave.alStatusText)},
        });
    }
    return result;
}

} // namespace Backend

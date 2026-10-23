#include "Ecat2CanfdController.h"
#include "Backend/Ethercat/BusSessionCoordinator.h"

#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace Backend {
namespace {
qint64 nowNs()
{ return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

QString stateName(quint16 state)
{
    QString name;
    switch (state & 0xf) {
    case 1: name = QStringLiteral("INIT"); break;
    case 2: name = QStringLiteral("PRE-OP"); break;
    case 4: name = QStringLiteral("SAFE-OP"); break;
    case 8: name = QStringLiteral("OP"); break;
    default: name = QStringLiteral("未知"); break;
    }
    return state & 0x10 ? name + QStringLiteral(" + ERROR") : name;
}

QString exceptionMessage()
{
    try { throw; }
    catch (const std::exception& e) { return QString::fromUtf8(e.what()); }
    catch (...) { return QStringLiteral("通信操作发生未知异常"); }
}
} // namespace

struct Ecat2CanfdController::RuntimeData {
    struct Periodic { canfd::Frame frame; qint64 intervalNs; qint64 nextNs; };
    std::mutex mutex;
    std::condition_variable cycleArrived;
    std::deque<canfd::Frame> outgoing;
    std::optional<Periodic> periodic;
    std::set<std::pair<int,int>> blocked;
    std::deque<CanFrameRecord> incoming;
    soem_interface::ApplicationCycle latest;
    quint64 lastSequence{0}, received{0}, dropped{0}, submitted{0};
    qint64 epochNs{nowNs()};
    bool hasCycle{false}, transmissionInterrupted{false};
};

Ecat2CanfdController::Ecat2CanfdController(BusSessionCoordinator& coordinator, QObject* parent)
    : Ecat2CanfdController(coordinator, makeCanfdSession(), parent) {}

Ecat2CanfdController::Ecat2CanfdController(BusSessionCoordinator& coordinator,
                                         std::unique_ptr<CanfdSession> session, QObject* parent)
    : QObject(parent), coordinator_(coordinator), session_(std::move(session)),
      runtime_(std::make_shared<RuntimeData>()), frames_(this)
{
    Q_ASSERT(session_);
    workers_.setMaxThreadCount(1);
    refreshTimer_.setInterval(33);
    connect(&refreshTimer_, &QTimer::timeout, this, &Ecat2CanfdController::refresh);
}

Ecat2CanfdController::~Ecat2CanfdController()
{
    refreshTimer_.stop(); cancel_.store(true);
    if (ownsSession_) {
        try { session_->requestStop(); } catch (...) {}
        workers_.start([session = session_.get()] { try { session->disconnectBus(); } catch (...) {} });
    }
    workers_.waitForDone();
    if (ownsSession_) coordinator_.release(BusSessionCoordinator::Mode::Ecat2Canfd);
}

QString Ecat2CanfdController::statusText() const
{
    if (state_ == "connecting") return QStringLiteral("正在连接并核对协议");
    if (state_ == "starting") return QStringLiteral("正在进入 OP");
    if (state_ == "stopping") return QStringLiteral("正在返回 PRE-OP");
    if (state_ == "disconnecting") return QStringLiteral("正在断开");
    if (state_ == "failed") return QStringLiteral("连接失败");
    if (state_ == "faulted") return QStringLiteral("已停止 · 状态未确认");
    if (running()) return busy_ ? QStringLiteral("OP · SDO 配置中") : QStringLiteral("OP · 运行中");
    if (connected_) return busy_ ? QStringLiteral("PRE-OP · SDO 配置中") : QStringLiteral("PRE-OP · 已连接");
    return QStringLiteral("未连接");
}

bool Ecat2CanfdController::canStart() const
{
    if (!connected_ || busy_ || state_ != "preop" || !protocolValid_ || configs_.size() != size_t(slaveCount() * 2)) return false;
    return std::all_of(configs_.begin(), configs_.end(), [](const auto& c) { return c.verified && c.status == 0 && c.error == 0; });
}

QVariantList Ecat2CanfdController::channelConfigs() const
{ QVariantList result; for (const auto& c : configs_) result.append(c.toMap()); return result; }

void Ecat2CanfdController::setNicName(const std::string& nic)
{ if (!active()) nicName_ = nic; }

void Ecat2CanfdController::setState(const QString& state, bool busy)
{ state_ = state; busy_ = busy; emit stateChanged(); }

void Ecat2CanfdController::fail(const QString& error)
{
    errorText_ = error;
    emit resultsChanged(); emit logAppended(error); emit errorOccurred(error);
}

void Ecat2CanfdController::connectBus(int expectedCount)
{
    if (active() || busy()) { fail(QStringLiteral("请先断开当前 ECAT2CANFD 会话")); return; }
    if (nicName_.empty() || expectedCount < 1 || expectedCount >= EC_MAXSLAVE) {
        fail(QStringLiteral("请选择网卡，并设置 1–%1 个预期从站").arg(EC_MAXSLAVE - 1)); return;
    }
    QString error;
    if (!coordinator_.tryAcquire(BusSessionCoordinator::Mode::Ecat2Canfd, error)) { fail(error); return; }
    ownsSession_ = true; connected_ = false; protocolValid_ = false; cancel_.store(false);
    errorText_.clear(); configs_.clear(); slaves_.clear(); diagnostics_.clear(); pdoSnapshot_.clear();
    selectedSlave_ = 0; clearFrames(); runtime_ = std::make_shared<RuntimeData>();
    const auto generation = ++generation_;
    setState(QStringLiteral("connecting"), true); emit resultsChanged();
    const QPointer<Ecat2CanfdController> self(this);
    workers_.start([this, self, generation, expectedCount, nic = nicName_] {
        soem_interface::BusScanResult scan;
        CanfdConfigResult configuration;
        QString error;
        bool valid = false;
        try {
            scan = session_->connectBus(nic);
            valid = canfd::validateScan(scan, expectedCount, error);
            if (valid) for (const auto& slave : scan.slaves) {
                if (cancel_.load() || !verifyCanfdIdentity(*session_, slave.position, error)) { valid = false; break; }
            }
            if (valid) {
                std::vector<int> positions;
                for (const auto& slave : scan.slaves) positions.push_back(slave.position);
                configuration = readCanfdConfiguration(*session_, positions, cancel_);
                if (!configuration.success) error = configuration.error;
            }
            if (!scan.success) session_->disconnectBus();
        } catch (...) {
            error = exceptionMessage(); scan.success = false;
            try { session_->disconnectBus(); } catch (...) {}
        }
        if (self) QMetaObject::invokeMethod(self, [self, generation, scan = std::move(scan), configuration = std::move(configuration), valid, error] {
            if (!self || generation != self->generation_) return;
            if (!scan.success) { self->finishDisconnect(generation, error); return; }
            self->connected_ = true; self->protocolValid_ = valid;
            for (const auto& slave : scan.slaves) {
                self->slaves_.append(QVariantMap{{"position", slave.position}, {"name", QString::fromStdString(slave.name)},
                    {"state", slave.state}, {"stateText", stateName(slave.state)}, {"alStatusCode", slave.alStatusCode},
                    {"alStatusText", QString::fromStdString(slave.alStatusText)}});
            }
            self->selectedSlave_ = scan.slaves.empty() ? 0 : scan.slaves.front().position;
            self->mergeConfigs(configuration.configs);
            self->setState(QStringLiteral("preop"), false);
            self->refreshTimer_.start();
            emit self->sessionConnected(scan.slaveCount);
            emit self->logAppended(QStringLiteral("已连接 %1 个从站，保持 PRE-OP").arg(scan.slaveCount));
            if (!error.isEmpty()) self->fail(error);
            emit self->resultsChanged();
        }, Qt::QueuedConnection);
    });
}

void Ecat2CanfdController::disconnectBus()
{
    if (!active() || state_ == "disconnecting") return;
    cancel_.store(true); clearOutgoing();
    const auto generation = ++generation_;
    setState(QStringLiteral("disconnecting"), true);
    try { session_->requestStop(); } catch (...) {}
    const QPointer<Ecat2CanfdController> self(this);
    workers_.start([this, self, generation] {
        QString error;
        try { session_->disconnectBus(); } catch (...) { error = exceptionMessage(); }
        if (self) QMetaObject::invokeMethod(self, [self, generation, error] { if (self) self->finishDisconnect(generation, error); }, Qt::QueuedConnection);
    });
}

void Ecat2CanfdController::finishDisconnect(quint64 generation, const QString& error)
{
    if (generation != generation_) return;
    refreshTimer_.stop(); clearOutgoing(); connected_ = false; protocolValid_ = false;
    if (ownsSession_) coordinator_.release(BusSessionCoordinator::Mode::Ecat2Canfd);
    ownsSession_ = false;
    setState(error.isEmpty() ? QStringLiteral("idle") : QStringLiteral("failed"), false);
    if (!pdoSnapshot_.isEmpty()) pdoSnapshot_["historical"] = true;
    emit sessionReleased(); emit resultsChanged();
    if (!error.isEmpty()) fail(error);
}

void Ecat2CanfdController::start(int cycleUs, bool dcEnabled, int shiftUs)
{
    if (!canStart()) { fail(QStringLiteral("请先连接匹配的从站，并成功回读或应用全部 CAN 配置")); return; }
    // Firmware TIM2 ticks every 1 ms and the independent output watchdog is 200 ticks.
    if (cycleUs < 250 || cycleUs > 100000 || qAbs(qint64(shiftUs)) >= cycleUs) {
        fail(QStringLiteral("总线周期必须为 250–100000 μs，偏移绝对值必须小于周期")); return;
    }
    cycleUs_ = cycleUs; dcEnabled_ = dcEnabled; cancel_.store(false); errorText_.clear(); clearOutgoing();
    {
        std::lock_guard<std::mutex> lock(runtime_->mutex);
        runtime_->lastSequence = 0; runtime_->hasCycle = false;
    }
    const auto generation = ++generation_;
    setState(QStringLiteral("starting"), true); emit resultsChanged();
    soem_interface::ApplicationRunOptions options;
    options.cycleUs = cycleUs; options.dcEnabled = dcEnabled; options.shiftUs = shiftUs;
    const QPointer<Ecat2CanfdController> self(this);
    workers_.start([this, self, generation, options, runtime = runtime_] {
        soem_interface::DcStartResult result;
        try {
            result = session_->start(options,
                [runtime](auto& images) { prepare(runtime, images); },
                [runtime](const auto& cycle) { consume(runtime, cycle); });
            if (!result.success) session_->stop();
        } catch (...) { result.error = exceptionMessage().toStdString(); try { session_->stop(); } catch (...) {} }
        if (self) QMetaObject::invokeMethod(self, [self, generation, result] {
            if (!self || generation != self->generation_) return;
            self->setState(result.success ? QStringLiteral("running") : QStringLiteral("preop"), false);
            if (result.success) emit self->logAppended(QStringLiteral("ECAT2CANFD 已进入 OP，空闲 PDO 保持无有效发送槽"));
            else if (!result.cancelled) self->fail(QString::fromStdString(result.error));
            self->refresh();
        }, Qt::QueuedConnection);
    });
}

void Ecat2CanfdController::stop()
{
    if (!running() && state_ != "starting") return;
    cancel_.store(true); clearOutgoing();
    const auto generation = ++generation_;
    setState(QStringLiteral("stopping"), true);
    try { session_->requestStop(); } catch (...) {}
    const QPointer<Ecat2CanfdController> self(this);
    workers_.start([this, self, generation] {
        QString error;
        try { session_->stop(); } catch (...) { error = exceptionMessage(); }
        if (self) QMetaObject::invokeMethod(self, [self, generation, error] {
            if (!self || generation != self->generation_) return;
            self->setState(QStringLiteral("preop"), false); self->refresh();
            if (!self->pdoSnapshot_.isEmpty()) self->pdoSnapshot_["historical"] = true;
            if (!error.isEmpty()) { self->setState(QStringLiteral("faulted"), false); self->fail(error); }
            emit self->logAppended(self->state_ == "preop"
                ? QStringLiteral("已停止周期通信，保留 PRE-OP 和 SDO 配置连接")
                : QStringLiteral("周期通信已停止，最终状态未确认；请断开后重新连接"));
            emit self->resultsChanged();
        }, Qt::QueuedConnection);
    });
}

void Ecat2CanfdController::mergeConfigs(const std::vector<canfd::ChannelConfig>& configs)
{
    for (const auto& config : configs) {
        auto existing = std::find_if(configs_.begin(), configs_.end(), [&](const auto& c) { return c.slave == config.slave && c.channel == config.channel; });
        if (existing == configs_.end()) configs_.push_back(config); else *existing = config;
    }
    std::sort(configs_.begin(), configs_.end(), [](const auto& a, const auto& b) { return std::pair(a.slave,a.channel) < std::pair(b.slave,b.channel); });
    emit resultsChanged(); emit stateChanged();
}

void Ecat2CanfdController::readConfiguration(int slave)
{
    if (!connected_ || busy_ || !protocolValid_) { fail(QStringLiteral("当前状态无法读取 ECAT2CANFD 配置")); return; }
    std::vector<int> positions;
    for (const auto& item : slaves_) {
        const int p = item.toMap().value("position").toInt();
        if (slave == 0 || slave == p) positions.push_back(p);
    }
    if (positions.empty()) { fail(QStringLiteral("选择的从站不存在")); return; }
    cancel_.store(false); busy_ = true; emit stateChanged();
    const auto generation = generation_;
    const QPointer<Ecat2CanfdController> self(this);
    workers_.start([this, self, generation, positions] {
        CanfdConfigResult result;
        try { result = readCanfdConfiguration(*session_, positions, cancel_); } catch (...) { result.error = exceptionMessage(); }
        if (self) QMetaObject::invokeMethod(self, [self, generation, result = std::move(result), positions] {
            if (!self || generation != self->generation_) return;
            if (!result.success) for (auto& c : self->configs_) if (std::find(positions.begin(), positions.end(), c.slave) != positions.end()) c.verified = false;
            self->busy_ = false; self->mergeConfigs(result.configs);
            if (!result.success) self->fail(result.error);
            else { self->errorText_.clear(); emit self->logAppended(QStringLiteral("CAN 配置回读完成")); emit self->resultsChanged(); }
        }, Qt::QueuedConnection);
    });
}

void Ecat2CanfdController::applyConfiguration(int slave, const QVariantList& configurations)
{
    if (!connected_ || busy_ || !protocolValid_ || configurations.empty()) { fail(QStringLiteral("当前状态无法应用 CAN 配置")); return; }
    const auto found = std::find_if(slaves_.begin(), slaves_.end(), [slave](const QVariant& item) { return item.toMap().value("position").toInt() == slave; });
    if (found == slaves_.end()) { fail(QStringLiteral("选择的从站不存在")); return; }
    std::vector<canfd::ChannelConfig> requested;
    std::set<int> channels;
    for (const auto& item : configurations) {
        canfd::ChannelConfig c; QString error;
        if (!canfd::parseConfig(slave, item.toMap(), c, error) || !channels.insert(c.channel).second) {
            fail(error.isEmpty() ? QStringLiteral("配置中 CAN 通道重复") : error); return;
        }
        requested.push_back(c);
    }
    for (const auto& c : requested) clearOutgoing(c.slave, c.channel);
    quint64 quietAfterSequence = 0;
    {
        std::lock_guard<std::mutex> lock(runtime_->mutex);
        for (const auto& c : requested) runtime_->blocked.insert({c.slave,c.channel});
        quietAfterSequence = runtime_->lastSequence;
    }
    for (auto& c : configs_) if (c.slave == slave && channels.count(c.channel)) c.verified = false;
    cancel_.store(false); busy_ = true; errorText_.clear(); emit stateChanged(); emit resultsChanged();
    const auto generation = generation_;
    const QPointer<Ecat2CanfdController> self(this);
    workers_.start([this, self, generation, requested, runtime = runtime_, quietAfterSequence, requireQuiet = running()] {
        CanfdConfigResult result;
        try {
            bool quiet = !requireQuiet;
            if (requireQuiet) {
                std::unique_lock<std::mutex> lock(runtime->mutex);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                auto targetsAreQuiet = [&] {
                    const auto& cycle = runtime->latest;
                    if (!runtime->hasCycle || cycle.sequence <= quietAfterSequence || cycle.expectedWorkingCounter <= 0
                        || cycle.workingCounter != cycle.expectedWorkingCounter) return false;
                    for (const auto& c : requested) {
                        const auto image = std::find_if(cycle.slaves.begin(), cycle.slaves.end(), [&](const auto& i) { return i.position == c.slave; });
                        if (image == cycle.slaves.end() || image->outputs.size() != canfd::ImageBytes) return false;
                        for (int slot = 0; slot < 9; ++slot)
                            if (canfd::readLe(image->outputs.data() + 4 + c.channel * 18 + slot * 2, 2) & canfd::ValidMask) return false;
                    }
                    return true;
                };
                while (!(quiet = targetsAreQuiet()) && !cancel_.load() && std::chrono::steady_clock::now() < deadline)
                    runtime->cycleArrived.wait_for(lock, std::chrono::milliseconds(50));
            }
            if (!quiet || cancel_.load()) result.error = QStringLiteral("未确认目标通道空 PDO 或操作已取消，未写入 CAN 配置");
            else result = applyCanfdConfiguration(*session_, requested, cancel_);
        } catch (...) { result.error = exceptionMessage(); }
        { std::lock_guard<std::mutex> lock(runtime->mutex); for (const auto& c : requested) runtime->blocked.erase({c.slave,c.channel}); }
        if (self) QMetaObject::invokeMethod(self, [self, generation, result = std::move(result)] {
            if (!self || generation != self->generation_) return;
            self->busy_ = false; self->mergeConfigs(result.configs);
            if (!result.success) self->fail(result.error);
            else emit self->logAppended(QStringLiteral("CAN 配置已逐项写入、应用并回读确认；发送保持停止"));
        }, Qt::QueuedConnection);
    });
}

void Ecat2CanfdController::selectSlave(int slave)
{
    if (std::none_of(slaves_.begin(), slaves_.end(), [slave](const QVariant& item) { return item.toMap().value("position").toInt() == slave; })) return;
    selectedSlave_ = slave; updatePdoSnapshot(); emit resultsChanged();
}

bool Ecat2CanfdController::makeFrame(int slave, int channel, const QString& id, int dlc, const QString& data, canfd::Frame& frame)
{
    if (!running() || busy_) { fail(QStringLiteral("请先启动 OP 周期通信，并等待配置操作完成")); return false; }
    bool exchangeHealthy = true;
    {
        std::lock_guard<std::mutex> lock(runtime_->mutex);
        exchangeHealthy = !runtime_->hasCycle || (runtime_->latest.expectedWorkingCounter > 0
            && runtime_->latest.workingCounter == runtime_->latest.expectedWorkingCounter);
    }
    if (!exchangeHealthy) { fail(QStringLiteral("当前 WKC 异常，发送已停止；请等待总线恢复后重新发送")); return false; }
    auto config = std::find_if(configs_.begin(), configs_.end(), [=](const auto& c) { return c.slave == slave && c.channel == channel; });
    if (config == configs_.end() || !config->verified || !config->enabled || config->status != 0 || config->error != 0) {
        fail(QStringLiteral("目标 CAN 通道未启用或配置未确认")); return false;
    }
    QString error;
    if (!canfd::parseFrame(slave, channel, id, dlc, data, frame, error)) { fail(error); return false; }
    return true;
}

void Ecat2CanfdController::sendOnce(int slave, int channel, const QString& id, int dlc, const QString& data)
{
    canfd::Frame frame;
    if (!makeFrame(slave, channel, id, dlc, data, frame)) return;
    bool full = false;
    {
        std::lock_guard<std::mutex> lock(runtime_->mutex);
        full = runtime_->outgoing.size() >= 128;
        if (!full) runtime_->outgoing.push_back(frame);
    }
    if (full) { fail(QStringLiteral("发送队列已满，请等待下一个周期")); return; }
    emit logAppended(QStringLiteral("已排队提交单帧：从站 %1 CAN%2 ID %3；无 CAN 端确认").arg(slave).arg(channel).arg(canfd::hex(frame.id,3)));
}

void Ecat2CanfdController::startPeriodic(int slave, int channel, const QString& id, int dlc, const QString& data, int intervalUs)
{
    canfd::Frame frame;
    if (!makeFrame(slave, channel, id, dlc, data, frame)) return;
    if (intervalUs <= 0 || intervalUs > 1000000000) { fail(QStringLiteral("发送间隔必须为 1–1000000000 μs")); return; }
    const qint64 rounded = ((qint64(intervalUs) + cycleUs_ - 1) / cycleUs_) * cycleUs_;
    periodicIntervalUs_ = int(rounded);
    {
        std::lock_guard<std::mutex> lock(runtime_->mutex);
        runtime_->periodic = RuntimeData::Periodic{frame, rounded * 1000, nowNs()};
    }
    periodicSending_ = true; emit stateChanged();
    emit logAppended(QStringLiteral("周期提交已启动，实际间隔 %1 μs").arg(periodicIntervalUs_));
}

void Ecat2CanfdController::stopPeriodic()
{
    { std::lock_guard<std::mutex> lock(runtime_->mutex); runtime_->periodic.reset(); }
    if (periodicSending_) { periodicSending_ = false; emit stateChanged(); }
}

void Ecat2CanfdController::clearOutgoing(int slave, int channel)
{
    bool periodicRemoved = false;
    {
        std::lock_guard<std::mutex> lock(runtime_->mutex);
        auto matches = [=](const canfd::Frame& f) { return (slave == 0 || slave == f.slave) && (channel < 0 || channel == f.channel); };
        auto& queue = runtime_->outgoing;
        queue.erase(std::remove_if(queue.begin(), queue.end(), matches), queue.end());
        if (runtime_->periodic && matches(runtime_->periodic->frame)) { runtime_->periodic.reset(); periodicRemoved = true; }
        if (slave == 0) runtime_->blocked.clear();
    }
    if (periodicRemoved || slave == 0) { periodicSending_ = false; emit stateChanged(); }
}

void Ecat2CanfdController::clearFrames()
{
    { std::lock_guard<std::mutex> lock(runtime_->mutex); runtime_->incoming.clear(); runtime_->received = runtime_->dropped = 0; }
    frames_.clear(); receivedCount_ = droppedCount_ = historyDropped_ = 0; emit resultsChanged();
}

void Ecat2CanfdController::prepare(const std::shared_ptr<RuntimeData>& runtime, std::vector<soem_interface::SlaveProcessImage>& images)
{
    std::lock_guard<std::mutex> lock(runtime->mutex);
    for (auto& image : images) std::fill(image.outputs.begin(), image.outputs.end(), 0);
    std::map<std::pair<int,int>, int> occupiedSlots;
    auto submit = [&](const canfd::Frame& frame) {
        const auto key = std::make_pair(frame.slave, frame.channel);
        if (runtime->blocked.count(key)) return;
        auto image = std::find_if(images.begin(), images.end(), [&](const auto& i) { return i.position == frame.slave; });
        if (image == images.end() || image->outputs.size() != canfd::ImageBytes) return;
        int& slot = occupiedSlots[key];
        if (slot >= canfd::SlotsPerChannel) return;
        canfd::encodeFrame(image->outputs, slot++, frame); ++runtime->submitted;
    };
    // A UI command is consumed once; a failed exchange must never replay it.
    if (!runtime->outgoing.empty()) { const auto frame = runtime->outgoing.front(); runtime->outgoing.pop_front(); submit(frame); }
    if (runtime->periodic) {
        const qint64 now = nowNs();
        if (now >= runtime->periodic->nextNs) {
            submit(runtime->periodic->frame);
            const qint64 elapsed = now - runtime->periodic->nextNs;
            runtime->periodic->nextNs += (elapsed / runtime->periodic->intervalNs + 1) * runtime->periodic->intervalNs;
        }
    }
}

void Ecat2CanfdController::consume(const std::shared_ptr<RuntimeData>& runtime, const soem_interface::ApplicationCycle& cycle)
{
    std::lock_guard<std::mutex> lock(runtime->mutex);
    if (runtime->hasCycle && cycle.sequence <= runtime->lastSequence) return;
    runtime->lastSequence = cycle.sequence; runtime->latest = cycle; runtime->hasCycle = true;
    runtime->cycleArrived.notify_all();
    if (cycle.expectedWorkingCounter <= 0 || cycle.workingCounter != cycle.expectedWorkingCounter) {
        runtime->transmissionInterrupted = runtime->transmissionInterrupted || !runtime->outgoing.empty() || runtime->periodic.has_value();
        runtime->outgoing.clear(); runtime->periodic.reset();
        return;
    }
    const qint64 timestampUs = std::max<qint64>(0, (cycle.hostTimeNs - runtime->epochNs) / 1000);
    for (const auto& image : cycle.slaves) for (const auto& frame : canfd::decodeValidFrames(image.position, image.inputs)) {
        if (runtime->incoming.size() == CanFrameModel::Capacity) { runtime->incoming.pop_front(); ++runtime->dropped; }
        runtime->incoming.push_back({timestampUs, frame}); ++runtime->received;
    }
}

void Ecat2CanfdController::updatePdoSnapshot()
{
    std::lock_guard<std::mutex> lock(runtime_->mutex);
    if (!runtime_->hasCycle) return;
    for (const auto& image : runtime_->latest.slaves) if (image.position == selectedSlave_) {
        pdoSnapshot_ = canfd::decodeImage(image);
        pdoSnapshot_["sequence"] = qulonglong(runtime_->latest.sequence);
        pdoSnapshot_["wkc"] = runtime_->latest.workingCounter;
        pdoSnapshot_["expectedWkc"] = runtime_->latest.expectedWorkingCounter;
        pdoSnapshot_["valid"] = runtime_->latest.expectedWorkingCounter > 0 && runtime_->latest.workingCounter == runtime_->latest.expectedWorkingCounter;
        pdoSnapshot_["historical"] = !running();
        return;
    }
}

void Ecat2CanfdController::refresh()
{
    std::deque<CanFrameRecord> batch;
    bool periodicActive = false, interrupted = false;
    {
        std::lock_guard<std::mutex> lock(runtime_->mutex);
        batch.swap(runtime_->incoming); receivedCount_ = runtime_->received; droppedCount_ = runtime_->dropped;
        diagnostics_["submittedCount"] = qulonglong(runtime_->submitted);
        periodicActive = runtime_->periodic.has_value();
        interrupted = runtime_->transmissionInterrupted; runtime_->transmissionInterrupted = false;
    }
    if (periodicSending_ != periodicActive) { periodicSending_ = periodicActive; emit stateChanged(); }
    if (interrupted) emit logAppended(QStringLiteral("WKC 异常：已清空发送队列并停止周期发送，恢复后需手动重新发送"));
    historyDropped_ += frames_.append(std::move(batch)); droppedCount_ += historyDropped_;
    updatePdoSnapshot();
    soem_interface::DcTestSnapshot snapshot;
    try { snapshot = session_->snapshot(); } catch (...) { fail(exceptionMessage()); stop(); return; }
    diagnostics_["cycleUs"] = cycleUs_;
    diagnostics_["wkc"] = snapshot.wkc; diagnostics_["expectedWkc"] = snapshot.expectedWkc;
    diagnostics_["cycleCount"] = qulonglong(snapshot.cycleCount);
    diagnostics_["wkcErrorCount"] = qulonglong(snapshot.wkcErrorCount);
    diagnostics_["missedCycles"] = qulonglong(snapshot.missedCycles);
    diagnostics_["dcEnabled"] = dcEnabled_;
    diagnostics_["dcTimeNs"] = QString::number(snapshot.dcTimeNs);
    diagnostics_["actualCycleUs"] = snapshot.actualCycleNs / 1000.0;
    diagnostics_["maxDeviationUs"] = snapshot.maxDeviationNs / 1000.0;
    diagnostics_["phaseErrorUs"] = snapshot.phaseErrorNs / 1000.0;
    diagnostics_["cleanupWarning"] = QString::fromStdString(snapshot.cleanupWarning);
    if (connected_ && !running() && !busy_ && snapshot.finished && !snapshot.cleanupWarning.empty() && state_ != "faulted")
        setState(QStringLiteral("faulted"), false);
    for (const auto& s : snapshot.slaves) for (auto& item : slaves_) {
        auto row = item.toMap();
        if (row.value("position").toInt() != s.address) continue;
        row["state"] = s.state; row["stateText"] = stateName(s.state);
        row["alStatusCode"] = s.alStatusCode; row["alStatusText"] = QString::fromStdString(s.alStatusText);
        item = row;
    }
    emit resultsChanged();
    if (running() && snapshot.finished) {
        if (!snapshot.error.empty()) fail(QString::fromStdString(snapshot.error));
        stop();
    }
}

} // namespace Backend

#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/ErrorText.h"
#include "ApplicationRuntime.h"
#include "Sync0Support.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace soem_interface::detail {

class ApplicationSoemWire final : public ApplicationWire {
    EcatMasterBus& bus_;
    ApplicationCycle cycle_;

    void captureStates(DcTestSnapshot& snapshot) {
        for (auto& slave : snapshot.slaves) {
            const auto& item = bus_.context_.slavelist[slave.address];
            slave.state = item.state;
            slave.alStatusCode = item.ALstatuscode;
            slave.alStatusText = alStatusText(item.ALstatuscode);
        }
    }

public:
    explicit ApplicationSoemWire(EcatMasterBus& bus) : bus_(bus) {}

    bool configure(const ApplicationRunOptions& options, DcTestSnapshot& s,
                   std::string& error) override {
        std::lock_guard<std::mutex> mailbox(bus_.mailboxMutex_);
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!bus_.master_init_ || !bus_.socket_open_ || !bus_.mapping_ready_ ||
            bus_.context_.slavecount <= 0 || bus_.running_) {
            error = "需要已完成扫描且空闲的 EtherCAT 应用会话。";
            return false;
        }
        if (ecx_readstate(&bus_.context_) != EC_STATE_PRE_OP) {
            error = "启动前，所有应用从站必须处于 PRE-OP。";
            return false;
        }
        for (int i = 1; i <= bus_.context_.slavecount; ++i) {
            if (bus_.context_.slavelist[i].state != EC_STATE_PRE_OP) {
                error = "从站 " + std::to_string(i) + " 未处于 PRE-OP。";
                return false;
            }
        }
        if (bus_.expectedWKC <= 0 || bus_.ioMapSize_ <= 0) {
            error = "应用没有已映射的过程数据。";
            return false;
        }
        // With blocked LRW, separate read/write paths can use twice the mapped
        // segment count. Reserve one frame for the serialized direct mailbox.
        const auto& group = bus_.context_.grouplist[0];
        const size_t processFrames = static_cast<size_t>(group.nsegments) *
                                     (group.blockLRW ? 2u : 1u);
        if (processFrames == 0 || processFrames + 1u > EC_MAXBUF) {
            error = "PDO 映射超出 SOEM 可同时处理的帧容量（包括 SDO）。";
            return false;
        }

        bus_.registeredCallbacksEnabled_ = false;
        bus_.disableCyclicMailboxesLocked();
        bus_.pendingPdoWrites_.clear();
        cycle_ = {};
        cycle_.expectedWorkingCounter = bus_.expectedWKC;
        s.expectedWkc = bus_.expectedWKC;
        s.slaves.clear();
        for (int i = 1; i <= bus_.context_.slavecount; ++i) {
            auto& item = bus_.context_.slavelist[i];
            SlaveProcessImage image;
            image.position = static_cast<uint16_t>(i);
            image.outputs.resize((static_cast<size_t>(item.Obits) + 7u) / 8u);
            image.inputs.resize((static_cast<size_t>(item.Ibits) + 7u) / 8u);
            if ((!image.outputs.empty() && !item.outputs) ||
                (!image.inputs.empty() && !item.inputs)) {
                error = "从站 " + std::to_string(i) + " 的 IO 映射不完整。";
                return false;
            }
            if (!image.outputs.empty()) std::memset(item.outputs, 0, image.outputs.size());
            cycle_.slaves.push_back(std::move(image));
            DcSlaveSnapshot slave;
            slave.address = static_cast<uint16_t>(i);
            slave.name = item.name;
            slave.hasDc = item.hasdc != 0;
            s.slaves.push_back(std::move(slave));
        }
        captureStates(s);
        if (options.dcEnabled) {
            if (!configureSync0(bus_.context_, s, error)) return false;
        } else {
            for (auto& slave : s.slaves) if (slave.hasDc) {
                slave.shutdownAttempted = true;
                slave.shutdownVerified = detail::disableSync0(bus_.context_, slave.address);
                if (!slave.shutdownVerified) {
                    error = "无法关闭 SYNC0，从站 " +
                            std::to_string(slave.address) + "。";
                    return false;
                }
            }
        }
        bus_.running_ = true;
        return true;
    }

    ApplicationPoll requestState(uint16_t state) override {
        std::unique_lock<std::mutex> mailbox(bus_.mailboxMutex_, std::try_to_lock);
        if (!mailbox.owns_lock()) return ApplicationPoll::Busy;
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!bus_.socket_open_ || !bus_.master_init_) return ApplicationPoll::Failed;
        bus_.context_.slavelist[0].state = state;
        return ecx_writestate(&bus_.context_, 0) > 0
            ? ApplicationPoll::Success : ApplicationPoll::Failed;
    }

    ApplicationPoll readStates(DcTestSnapshot& snapshot) override {
        std::unique_lock<std::mutex> mailbox(bus_.mailboxMutex_, std::try_to_lock);
        if (!mailbox.owns_lock()) return ApplicationPoll::Busy;
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!bus_.socket_open_ || !bus_.master_init_) return ApplicationPoll::Failed;
        const auto state = ecx_readstate(&bus_.context_);
        captureStates(snapshot);
        bus_.updateStateFlagsLocked();
        return state != EC_STATE_NONE ? ApplicationPoll::Success : ApplicationPoll::Failed;
    }

    DcExchange exchange(const PrepareOutputs& prepare, const ConsumeInputs& consume,
                        bool applicationEnabled) override {
        for (auto& image : cycle_.slaves)
            std::fill(image.outputs.begin(), image.outputs.end(), 0);
        if (applicationEnabled && prepare) prepare(cycle_.slaves);
        {
            std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
            if (!applicationEnabled) {
                // A rejected callback may have corrupted the image shape.
                // Cleanup must still send neutral data and reach PRE-OP.
                cycle_.slaves.resize(static_cast<size_t>(bus_.context_.slavecount));
                for (size_t i = 0; i < cycle_.slaves.size(); ++i) {
                    auto& image = cycle_.slaves[i];
                    const auto& item = bus_.context_.slavelist[i + 1];
                    image.position = static_cast<uint16_t>(i + 1);
                    image.outputs.assign((static_cast<size_t>(item.Obits) + 7u) / 8u, 0);
                    image.inputs.resize((static_cast<size_t>(item.Ibits) + 7u) / 8u);
                }
            }
            if (cycle_.slaves.size() != static_cast<size_t>(bus_.context_.slavecount))
                throw std::runtime_error("应用回调改变了从站过程映像的数量。");
            for (size_t i = 0; i < cycle_.slaves.size(); ++i) {
                const auto& image = cycle_.slaves[i];
                auto& item = bus_.context_.slavelist[i + 1];
                if (image.position != i + 1 ||
                    image.outputs.size() != (static_cast<size_t>(item.Obits) + 7u) / 8u ||
                    image.inputs.size() != (static_cast<size_t>(item.Ibits) + 7u) / 8u)
                    throw std::runtime_error("应用回调改变了已映射过程映像的布局。");
                if (!image.outputs.empty())
                    std::memcpy(item.outputs, image.outputs.data(), image.outputs.size());
            }
            bus_.context_.DCtime = 0;
            const int sent = ecx_send_processdata(&bus_.context_);
            cycle_.workingCounter = sent > 0
                ? ecx_receive_processdata(&bus_.context_, EC_TIMEOUTRET) : EC_NOFRAME;
            bus_.wkc = cycle_.workingCounter;
            cycle_.dcTimeNs = static_cast<int64_t>(bus_.context_.DCtime);
            cycle_.hostTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            ++cycle_.sequence;
            for (auto& image : cycle_.slaves) {
                const auto& item = bus_.context_.slavelist[image.position];
                if (!image.inputs.empty()) {
                    if (cycle_.workingCounter == cycle_.expectedWorkingCounter)
                        std::memcpy(image.inputs.data(), item.inputs, image.inputs.size());
                    else
                        std::fill(image.inputs.begin(), image.inputs.end(), 0);
                }
            }
        }
        // No callbacks run while the transport context is
        // locked. The cycle images themselves belong exclusively to this worker.
        if (applicationEnabled && consume) consume(cycle_);
        return {cycle_.workingCounter, cycle_.dcTimeNs,
                cycle_.workingCounter >= 0 && cycle_.dcTimeNs > 0};
    }

    bool disableSync0(uint16_t slave) override {
        std::lock_guard<std::mutex> mailbox(bus_.mailboxMutex_);
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        return bus_.socket_open_ && detail::disableSync0(bus_.context_, slave);
    }

    void finish() override {
        std::lock_guard<std::mutex> mailbox(bus_.mailboxMutex_);
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        bus_.running_ = false;
        bus_.pendingPdoWrites_.clear();
        for (int i = 1; i <= bus_.context_.slavecount; ++i) {
            auto& item = bus_.context_.slavelist[i];
            const size_t bytes = (static_cast<size_t>(item.Obits) + 7u) / 8u;
            if (item.outputs && bytes) std::memset(item.outputs, 0, bytes);
        }
        bus_.updateStateFlagsLocked();
    }
};

} // namespace soem_interface::detail

namespace soem_interface {

void EcatMasterBus::initializeApplicationRuntime() {
    applicationRuntime_ = std::make_unique<detail::ApplicationRuntime>(
        std::make_unique<detail::ApplicationSoemWire>(*this));
}

DcStartResult EcatMasterBus::startApplication(const ApplicationRunOptions& options,
                                             PrepareOutputs prepare, ConsumeInputs consume) {
    if (dcRuntime_->active() || running_)
        return {false, false, "已有其他过程数据任务正在运行。"};
    return applicationRuntime_->start(options, std::move(prepare), std::move(consume));
}

void EcatMasterBus::requestApplicationStop() { applicationRuntime_->requestStop(); }
void EcatMasterBus::stopApplication() { applicationRuntime_->stop(); }
DcTestSnapshot EcatMasterBus::applicationSnapshot() const { return applicationRuntime_->snapshot(); }

} // namespace soem_interface

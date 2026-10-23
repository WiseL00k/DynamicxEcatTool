#include "SOEM_interface/EcatMasterBus.h"
#include "SOEM_interface/ErrorText.h"
#include "DcRuntime.h"
#include "Sync0Support.h"
#include <algorithm>
#include <cstring>

namespace soem_interface::detail {
class DcSoemWire final : public DcWire {
    EcatMasterBus& bus_;
    bool ownsSocket_{false};
public:
    explicit DcSoemWire(EcatMasterBus& bus) : bus_(bus) {}
    bool discover(DcTestSnapshot& s, std::string& error) override {
        std::lock_guard<std::mutex> mailbox(bus_.mailboxMutex_);
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (bus_.socket_open_ || bus_.running_) { error = "网卡已被占用。"; return false; }
        if (bus_.nic_name_.empty()) { error = "网卡名称为空。"; return false; }
        bus_.registeredCallbacksEnabled_ = false;
        bus_.pendingPdoWrites_.clear();
        bus_.context_.manualstatechange = 1;
        if (ecx_init(&bus_.context_, bus_.nic_name_.c_str()) <= 0) {
            error = "SOEM 无法打开所选网卡。"; return false;
        }
        ownsSocket_ = true; bus_.socket_open_ = true;
        bus_.context_.manualstatechange = 1;
        if (ecx_config_init(&bus_.context_) <= 0) { error = "未发现 EtherCAT 从站。"; return false; }
        bus_.master_init_ = true;
        for (int i = 1; i <= bus_.context_.slavecount; ++i) {
            auto& item = bus_.context_.slavelist[i];
            item.PO2SOconfig = nullptr;
            DcSlaveSnapshot slave;
            slave.address = static_cast<uint16_t>(i);
            slave.name = item.name; slave.hasDc = item.hasdc != 0;
            s.slaves.push_back(std::move(slave));
        }
        return true;
    }
    bool configure(DcTestSnapshot& s, std::string& error) override {
        std::lock_guard<std::mutex> mailbox(bus_.mailboxMutex_);
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!bus_.mapProcessDataLocked(error)) return false;
        if (bus_.ioMapSize_ <= 0 || bus_.expectedWKC <= 0) { error = "总线没有已映射的 PDO 数据。"; return false; }
        s.expectedWkc = bus_.expectedWKC;
        std::fill(bus_.ioMap_.begin(), bus_.ioMap_.end(), 0);
        // No application callbacks, vendor SDOs or cyclic mailboxes in the DC test.
        bus_.disableCyclicMailboxesLocked();
        return configureSync0(bus_.context_, s, error);
    }
    bool requestState(uint16_t state) override {
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!ownsSocket_ || bus_.context_.slavecount <= 0) return false;
        bus_.context_.slavelist[0].state = state;
        return ecx_writestate(&bus_.context_, 0) > 0;
    }
    bool readStates(DcTestSnapshot& s) override {
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!ownsSocket_) return false;
        const auto state = ecx_readstate(&bus_.context_);
        for (auto& slave : s.slaves) {
            const auto& item = bus_.context_.slavelist[slave.address];
            slave.state = item.state; slave.alStatusCode = item.ALstatuscode;
            slave.alStatusText = alStatusText(item.ALstatuscode);
        }
        bus_.updateStateFlagsLocked();
        return state != EC_STATE_NONE;
    }
    DcExchange exchange() override {
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        bus_.running_ = true;
        for (int i = 1; i <= bus_.context_.slavecount; ++i) {
            auto& slave = bus_.context_.slavelist[i];
            const size_t bytes = (static_cast<size_t>(slave.Obits) + 7u) / 8u;
            if (slave.outputs && bytes) std::memset(slave.outputs, 0, bytes);
        }
        // A failed DC datagram must not reuse an earlier timestamp.
        bus_.context_.DCtime = 0;
        const int sent = ecx_send_processdata(&bus_.context_);
        const int received = sent > 0 ? ecx_receive_processdata(&bus_.context_, EC_TIMEOUTRET) : -1;
        bus_.wkc = received;
        return {received, static_cast<int64_t>(bus_.context_.DCtime),
                sent > 0 && received >= 0 && bus_.context_.DCtime > 0};
    }
    bool disableSync0(uint16_t slave) override {
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!ownsSocket_) return false;
        return detail::disableSync0(bus_.context_, slave);
    }
    void close() override {
        std::lock_guard<std::mutex> mailbox(bus_.mailboxMutex_);
        std::lock_guard<std::recursive_mutex> context(bus_.contextMutex_);
        if (!ownsSocket_) return;
        ecx_close(&bus_.context_);
        std::memset(&bus_.context_, 0, sizeof(bus_.context_));
        ownsSocket_ = false;
        bus_.socket_open_ = false; bus_.master_init_ = false; bus_.mapping_ready_ = false;
        bus_.running_ = false; bus_.operational_ = false; bus_.pre_op_ = false; bus_.init_ = false;
        bus_.ioMapSize_ = 0; bus_.expectedWKC = 0; bus_.wkc = 0;
        bus_.pendingPdoWrites_.clear();
    }
};
} // namespace soem_interface::detail

namespace soem_interface {
void EcatMasterBus::initializeDcRuntime() {
    dcRuntime_ = std::make_unique<detail::DcRuntime>(std::make_unique<detail::DcSoemWire>(*this));
}
DcStartResult EcatMasterBus::startDcTest(const DcTestOptions& options) {
    if (running_ || socket_open_) return {false, false, "EtherCAT 网卡已被占用。"};
    return dcRuntime_->start(options);
}
void EcatMasterBus::requestDcStop() { dcRuntime_->requestStop(); }
void EcatMasterBus::stopDcTest() { dcRuntime_->stop(); }
DcTestSnapshot EcatMasterBus::dcTestSnapshot() const { return dcRuntime_->snapshot(); }
void EcatMasterBus::resetDcStatistics() { dcRuntime_->resetStatistics(); }
} // namespace soem_interface

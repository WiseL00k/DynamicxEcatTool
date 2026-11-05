#include "CanfdSession.h"
#include <QThread>
#include <chrono>
#include <exception>
#include <mutex>
#include <stdexcept>

namespace Backend {
namespace {

class MasterCanfdSession final : public CanfdSession
{
public:
    soem_interface::BusScanResult connectBus(const std::string& nic) override
    {
        auto bus = std::make_shared<soem_interface::EcatMasterBus>(nic);
        { std::lock_guard<std::mutex> lock(mutex_); bus_ = bus; }
        return bus->scanForSlaves();
    }
    void disconnectBus() override
    {
        std::shared_ptr<soem_interface::EcatMasterBus> bus;
        { std::lock_guard<std::mutex> lock(mutex_); bus = std::move(bus_); }
        std::exception_ptr failure;
        if (bus) {
            try { bus->stopApplication(); } catch (...) { failure = std::current_exception(); }
            try {
                const auto snapshot = bus->applicationSnapshot();
                if (!failure && !snapshot.cleanupWarning.empty()) {
                    failure = std::make_exception_ptr(std::runtime_error(
                        "EtherCAT 停止状态未完全确认：" + snapshot.cleanupWarning));
                }
            } catch (...) { if (!failure) failure = std::current_exception(); }
            // Always close the socket even if the application stop reports a failure.
            try { bus->resetExplorer(); } catch (...) { if (!failure) failure = std::current_exception(); }
        }
        bus.reset();
        if (failure) std::rethrow_exception(failure);
    }
    soem_interface::DcStartResult start(const soem_interface::ApplicationRunOptions& options,
                                       soem_interface::PrepareOutputs prepare,
                                       soem_interface::ConsumeInputs consume) override
    {
        auto bus = current();
        return bus ? bus->startApplication(options, std::move(prepare), std::move(consume))
                   : soem_interface::DcStartResult{false, false, "EtherCAT 会话已断开"};
    }
    void requestStop() override { if (auto bus = current()) bus->requestApplicationStop(); }
    void stop() override { if (auto bus = current()) bus->stopApplication(); }
    soem_interface::DcTestSnapshot snapshot() const override
    { auto bus = current(); return bus ? bus->applicationSnapshot() : soem_interface::DcTestSnapshot{}; }
    soem_interface::SdoTransferResult readSdo(int slave, quint16 index, quint8 subindex, int size, void* data) override
    {
        auto bus = current();
        if (bus) return bus->sdoReadDetailed(quint16(slave), index, subindex, false, size, data);
        soem_interface::SdoTransferResult r; r.error = "EtherCAT 会话已断开"; return r;
    }
    soem_interface::SdoTransferResult writeSdo(int slave, quint16 index, quint8 subindex, const std::vector<quint8>& data) override
    {
        auto bus = current();
        if (bus) return bus->sdoWriteDetailed(quint16(slave), index, subindex, false, int(data.size()), data.data());
        soem_interface::SdoTransferResult r; r.error = "EtherCAT 会话已断开"; return r;
    }
private:
    std::shared_ptr<soem_interface::EcatMasterBus> current() const
    { std::lock_guard<std::mutex> lock(mutex_); return bus_; }
    mutable std::mutex mutex_;
    std::shared_ptr<soem_interface::EcatMasterBus> bus_;
};

QString transferError(int slave, quint16 index, quint8 subindex,
                      const soem_interface::SdoTransferResult& result)
{
    return QStringLiteral("从站 %1 SDO 0x%2:%3 失败（WKC %4，终止码 0x%5）：%6")
        .arg(slave).arg(canfd::hex(index, 4)).arg(canfd::hex(subindex, 2))
        .arg(result.wkc).arg(canfd::hex(result.abortCode, 8)).arg(QString::fromStdString(result.error));
}

bool readValue(CanfdSession& session, int slave, quint16 index, quint8 subindex,
               int size, quint32& value, QString& error)
{
    std::array<quint8, 4> bytes{};
    const auto result = session.readSdo(slave, index, subindex, size, bytes.data());
    if (!result.success || result.actualSize != size) {
        error = transferError(slave, index, subindex, result);
        if (result.success) error += QStringLiteral("；返回长度 %1，期望 %2").arg(result.actualSize).arg(size);
        return false;
    }
    value = canfd::readLe(bytes.data(), size);
    return true;
}

bool readChannel(CanfdSession& session, int slave, int channel, canfd::ChannelConfig& c, QString& error)
{
    c = {}; c.slave = slave; c.channel = channel;
    quint32 values[9]{};
    const int sizes[9]{1, 1, 1, 4, 4, 2, 1, 1, 4};
    for (int sub = 1; sub <= 8; ++sub) {
        if (!readValue(session, slave, quint16(0x2000 + channel), quint8(sub), sizes[sub], values[sub], error)) return false;
    }
    if (values[1] > 1 || values[2] > 2 || values[5] > 7 || values[7] > 3) {
        error = QStringLiteral("从站 %1 CAN%2 返回了固件协议以外的配置值").arg(slave).arg(channel); return false;
    }
    c.enabled = values[1] != 0; c.format = int(values[2]); c.nominalBitrate = values[3];
    c.dataBitrate = values[4]; c.options = quint16(values[5]); c.status = quint8(values[7]); c.error = values[8];
    c.verified = canfd::validConfig(c, error);
    return c.verified;
}

} // namespace

std::unique_ptr<CanfdSession> makeCanfdSession() { return std::make_unique<MasterCanfdSession>(); }

bool verifyCanfdIdentity(CanfdSession& session, int slave, QString& error)
{
    const quint32 expected[]{0, 0, 0xAB125, 3};
    for (quint8 sub = 1; sub <= 3; ++sub) {
        quint32 actual = 0;
        if (!readValue(session, slave, 0x1018, sub, 4, actual, error)) return false;
        if (actual != expected[sub]) {
            error = QStringLiteral("从站 %1 在线身份 0x1018:%2 不匹配 ECAT2CANFD").arg(slave).arg(sub);
            return false;
        }
    }
    return true;
}

CanfdConfigResult readCanfdConfiguration(CanfdSession& session, const std::vector<int>& slaves,
                                        const std::atomic<bool>& cancel)
{
    CanfdConfigResult result;
    for (int slave : slaves) for (int channel = 0; channel < 2; ++channel) {
        if (cancel.load()) { result.error = QStringLiteral("配置读取已取消"); return result; }
        canfd::ChannelConfig c;
        if (!readChannel(session, slave, channel, c, result.error)) return result;
        result.configs.push_back(c);
    }
    result.success = true;
    return result;
}

CanfdConfigResult applyCanfdConfiguration(CanfdSession& session,
                                         const std::vector<canfd::ChannelConfig>& configs,
                                         const std::atomic<bool>& cancel)
{
    CanfdConfigResult result;
    for (const auto& requested : configs) {
        if (!canfd::validConfig(requested, result.error)) return result;
        const quint16 index = quint16(0x2000 + requested.channel);
        const quint32 values[]{0, quint32(requested.enabled), quint32(requested.format),
                               requested.nominalBitrate, requested.dataBitrate, requested.options, 1};
        const int sizes[]{0, 1, 1, 4, 4, 2, 1};
        bool applied = true;
        for (int sub = 1; sub <= 6; ++sub) {
            if (cancel.load()) { result.error = QStringLiteral("配置写入已取消；已写字段需重新回读确认"); applied = false; break; }
            auto transfer = session.writeSdo(requested.slave, index, quint8(sub), canfd::writeLe(values[sub], sizes[sub]));
            if (!transfer.success) {
                result.error = transferError(requested.slave, index, quint8(sub), transfer);
                applied = false; break;
            }
        }
        if (applied) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (;;) {
                quint32 status = 0;
                if (cancel.load()) { result.error = QStringLiteral("配置应用等待已取消；结果需回读确认"); applied = false; break; }
                if (!readValue(session, requested.slave, index, 7, 1, status, result.error)) { applied = false; break; }
                if (status == 0) break;
                if (status != 2 || std::chrono::steady_clock::now() >= deadline) {
                    result.error = QStringLiteral("从站 %1 CAN%2 配置%3（状态 %4）")
                        .arg(requested.slave).arg(requested.channel).arg(status == 2 ? QStringLiteral("应用超时") : QStringLiteral("未成功应用")).arg(status);
                    applied = false; break;
                }
                QThread::msleep(50);
            }
        }
        canfd::ChannelConfig actual;
        QString readError;
        if (readChannel(session, requested.slave, requested.channel, actual, readError)) {
            result.configs.push_back(actual);
            if (applied && (actual.status != 0 || actual.error != 0 || actual.enabled != requested.enabled
                || actual.format != requested.format || actual.nominalBitrate != requested.nominalBitrate
                || actual.dataBitrate != requested.dataBitrate || actual.options != requested.options)) {
                applied = false;
                result.error = QStringLiteral("从站 %1 CAN%2 配置回读不一致，状态 %3，错误 0x%4")
                    .arg(requested.slave).arg(requested.channel).arg(actual.status).arg(canfd::hex(actual.error, 8));
            } else if (!applied && actual.error != 0) {
                result.error += QStringLiteral("；固件错误 0x%1").arg(canfd::hex(actual.error, 8));
            }
        } else {
            actual = requested; actual.verified = false; result.configs.push_back(actual);
            if (result.error.isEmpty()) result.error = readError;
            else result.error += QStringLiteral("；回读失败：") + readError;
            applied = false;
        }
        if (!applied) {
            if (&requested != &configs.back()) result.error += QStringLiteral("；后续通道未执行");
            return result;
        }
    }
    result.success = true;
    return result;
}

} // namespace Backend

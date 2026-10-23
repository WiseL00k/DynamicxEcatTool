#include "DamiaoMitController.h"
#include "MitMotorCommand.h"
#include <QRegularExpression>

namespace Backend {

DamiaoMitController::DamiaoMitController(BusSessionCoordinator& coordinator, QObject* parent)
    : DamiaoMitController(coordinator, makeCanfdSession(), parent) {}

DamiaoMitController::DamiaoMitController(BusSessionCoordinator& coordinator,
                                       std::unique_ptr<CanfdSession> session, QObject* parent)
    : QObject(parent), coordinator_(coordinator),
      canfd_(coordinator, std::move(session), BusSessionCoordinator::Mode::MitDebug, true, this)
{
    connect(&canfd_, &Ecat2CanfdController::stateChanged, this, &DamiaoMitController::stateChanged);
    connect(&canfd_, &Ecat2CanfdController::resultsChanged, this, [this] {
        if (errorFromCanfd_ && canfd_.errorText().isEmpty()) {
            errorText_.clear(); errorFromCanfd_ = false;
        }
        emit stateChanged();
    });
    connect(&canfd_, &Ecat2CanfdController::logAppended, this, &DamiaoMitController::logAppended);
    connect(&canfd_, &Ecat2CanfdController::errorOccurred, this, [this](const QString& error) {
        // The inner controller has already logged this error exactly once.
        errorText_ = error; errorFromCanfd_ = true;
        emit stateChanged(); emit errorOccurred(error);
    });
}

QString DamiaoMitController::state() const
{ return gateway() ? canfd_.state() : legacyBusy_ ? QStringLiteral("connecting") : legacyConnected_ ? QStringLiteral("running") : QStringLiteral("idle"); }
bool DamiaoMitController::active() const { return gateway() ? canfd_.active() : legacyConnected_ || legacyBusy_; }
bool DamiaoMitController::connected() const { return gateway() ? canfd_.connected() : legacyConnected_; }
bool DamiaoMitController::busy() const { return gateway() ? canfd_.busy() : legacyBusy_; }
bool DamiaoMitController::running() const { return gateway() ? canfd_.running() : legacyConnected_; }
bool DamiaoMitController::canStart() const { return gateway() && canfd_.canStart(); }
bool DamiaoMitController::periodicSending() const { return gateway() && canfd_.periodicSending(); }
int DamiaoMitController::periodicIntervalUs() const { return gateway() ? canfd_.periodicIntervalUs() : 0; }
bool DamiaoMitController::targetLocked() const { return gateway() && canfd_.hasPendingTransmission(); }
QString DamiaoMitController::targetIdHex() const { return canfd::hex(targetId_, 3); }
QString DamiaoMitController::statusText() const
{
    if (gateway()) return canfd_.statusText();
    if (legacyBusy_) return QStringLiteral("正在连接原 MIT 从站");
    return legacyConnected_ ? QStringLiteral("原 MIT 从站 · OP") : QStringLiteral("未连接");
}

void DamiaoMitController::setNicName(const std::string& nic) { canfd_.setNicName(nic); }
void DamiaoMitController::setLegacyConnected(bool connected)
{
    if (legacyConnected_ == connected) return;
    legacyConnected_ = connected;
    emit stateChanged();
}
void DamiaoMitController::reportLegacyError(const QString& error)
{
    // The legacy backend already emitted its global error signal. Only mirror
    // the diagnostic here, without forwarding it back into that same signal.
    if (!gateway() && (legacyBusy_ || legacyConnected_)) { errorText_ = error; errorFromCanfd_ = false; emit stateChanged(); }
}
bool DamiaoMitController::fail(const QString& message)
{
    errorText_ = message; errorFromCanfd_ = false;
    emit stateChanged(); emit logAppended(message); emit errorOccurred(message);
    return false;
}

bool DamiaoMitController::setSlaveType(const QString& type)
{
    if (type != QStringLiteral("mit") && type != QStringLiteral("ecat2canfd"))
        return fail(QStringLiteral("请选择原 MIT 或 ECAT2CANFD 从站"));
    if (type == slaveType_) return true;
    if (active() || canfd_.active() || legacyConnected_ || legacyBusy_)
        return fail(QStringLiteral("请先断开当前连接，再切换从站类型"));
    slaveType_ = type; errorText_.clear(); emit stateChanged();
    return true;
}

void DamiaoMitController::connectBus()
{
    if (active()) { fail(QStringLiteral("当前调试会话已连接或正在连接")); return; }
    errorText_.clear();
    if (gateway()) { canfd_.connectBus(1); return; }
    if (coordinator_.active()) { fail(QStringLiteral("总线被其他会话占用，请先断开")); return; }
    legacyBusy_ = true; emit stateChanged();
    emit legacyConnectRequested();
    legacyBusy_ = false; emit stateChanged();
    if (!legacyConnected_ && errorText_.isEmpty()) fail(QStringLiteral("原 MIT 从站连接未完成"));
}

void DamiaoMitController::disconnectBus()
{
    if (gateway()) canfd_.disconnectBus();
    else if (legacyConnected_) emit legacyDisconnectRequested();
}

void DamiaoMitController::start(int cycleUs, bool dcEnabled, int shiftUs)
{
    if (!gateway()) { if (!connected()) fail(QStringLiteral("请先连接原 MIT 从站")); return; }
    errorText_.clear(); canfd_.start(cycleUs, dcEnabled, shiftUs);
}

void DamiaoMitController::stop()
{
    if (gateway()) { canfd_.cancelPendingTransmission(); canfd_.stop(); }
    else disconnectBus();
}

QVariantMap DamiaoMitController::preview(const QVariantMap& values) const { return previewMitFrame(values); }

bool DamiaoMitController::parseTarget(int channel, const QString& text, int& id)
{
    if (channel < 0 || channel > 1) return fail(QStringLiteral("CAN 通道必须为 0 或 1"));
    auto normalized = text.trimmed();
    if (gateway() && normalized.startsWith("0x", Qt::CaseInsensitive)) normalized.remove(0, 2);
    const QRegularExpression pattern(gateway() ? QStringLiteral("^[0-9a-fA-F]{1,3}$") : QStringLiteral("^[1-9]$"));
    bool ok = false; id = normalized.toInt(&ok, gateway() ? 16 : 10);
    if (!ok || !pattern.match(normalized).hasMatch() || id > (gateway() ? 0x7ff : 9))
        return fail(gateway() ? QStringLiteral("CAN ID 必须是 000–7FF 的十六进制数") : QStringLiteral("原 MIT CAN 槽位 ID 必须为十进制 1–9"));
    if (targetLocked() && (targetChannel_ != channel || targetId_ != id))
        return fail(QStringLiteral("当前目标仍有发送任务，请先停止发送后再切换 CAN 通道或 ID"));
    return true;
}

bool DamiaoMitController::ready(int channel)
{
    if (!running() || busy() || coordinator_.mode() != BusSessionCoordinator::Mode::MitDebug)
        return fail(QStringLiteral("请先连接并进入 OP，等待配置操作完成"));
    if (!gateway()) return true;
    for (const auto& item : canfd_.channelConfigs()) {
        const auto c = item.toMap();
        if (c.value("slave").toInt() != 1 || c.value("channel").toInt() != channel) continue;
        if (c.value("verified").toBool() && c.value("enabled").toBool() &&
            c.value("status").toInt() == 0 && c.value("error").toUInt() == 0)
            return true;
    }
    return fail(QStringLiteral("达妙 MIT 控制要求目标 CAN 通道已启用，且配置回读确认成功"));
}

bool DamiaoMitController::sendControl(int channel, const QString& id, const QVariantMap& values,
                                     bool periodic, int intervalUs)
{
    const auto frame = preview(values);
    if (!frame.value("valid").toBool()) return fail(frame.value("error").toString());
    return sendRaw(channel, id, frame.value("hex").toString(), periodic, intervalUs);
}

bool DamiaoMitController::sendRaw(int channel, const QString& idText, const QString& data,
                                 bool periodic, int intervalUs)
{
    int id = 0;
    if (!parseTarget(channel, idText, id) || !ready(channel)) return false;
    canfd::Frame frame; QString error;
    if (!canfd::parseFrame(1, channel, canfd::hex(id, 3), 8, data, frame, error)) return fail(error);
    if (!gateway() && periodic) return fail(QStringLiteral("原 MIT 从站使用原有持续槽位控制，无需另启周期提交"));
    errorText_.clear();
    if (gateway()) {
        if (!canfd_.submitExclusive(1, channel, canfd::hex(id, 3), data, periodic, intervalUs)) return false;
        targetChannel_ = channel; targetId_ = id;
        emit logAppended(QStringLiteral("已提交%1：CAN%2 ID %3，%4；仅表示发送任务已提交")
            .arg(periodic ? QStringLiteral("周期控制") : QStringLiteral("单次控制")).arg(channel)
            .arg(canfd::hex(id, 3), data.trimmed().toUpper()));
    } else {
        QVariantList bytes; for (auto byte : frame.data) bytes.append(byte);
        emit legacySendRequested(channel, id, bytes);
        if (!errorText_.isEmpty()) return false;
        emit logAppended(QStringLiteral("已更新原 MIT 从站 CAN%1 槽位 %2；沿用持续槽位控制").arg(channel).arg(id));
    }
    emit stateChanged(); return true;
}

bool DamiaoMitController::special(int channel, const QString& idText, quint8 opcode)
{
    if (!gateway()) {
        // Legacy enable/disable applies to the whole slave. An incomplete
        // target editor must never prevent its global disable command.
        if (!ready(0)) return false;
        errorText_.clear();
        if (opcode == 0xfc) emit legacyEnableRequested(); else emit legacyDisableRequested();
        if (!errorText_.isEmpty()) return false;
        emit logAppended(opcode == 0xfc ? QStringLiteral("已提交原 MIT 从站全部电机使能命令")
                                      : QStringLiteral("已提交原 MIT 从站全部电机失能命令"));
        return true;
    }
    int id = 0;
    if (!parseTarget(channel, idText, id) || !ready(channel)) return false;
    errorText_.clear();
    const auto data = QStringLiteral("FF FF FF FF FF FF FF %1").arg(canfd::hex(opcode, 2));
    if (!canfd_.submitExclusive(1, channel, canfd::hex(id, 3), data, false, 0, true)) return false;
    targetChannel_ = channel; targetId_ = id;
    emit stateChanged();
    emit logAppended(QStringLiteral("已清除旧发送任务，空 PDO 后提交 CAN%1 ID %2 的%3帧；未确认电机执行")
        .arg(channel).arg(canfd::hex(id, 3), opcode == 0xfc ? QStringLiteral("使能 FC") : QStringLiteral("失能 FD")));
    return true;
}

bool DamiaoMitController::enableMotor(int channel, const QString& id) { return special(channel, id, 0xfc); }
bool DamiaoMitController::disableMotor(int channel, const QString& id) { return special(channel, id, 0xfd); }

bool DamiaoMitController::stopSending(int channel, const QString& idText)
{
    int id = 0;
    if (!parseTarget(channel, idText, id)) return false;
    if (!connected() || coordinator_.mode() != BusSessionCoordinator::Mode::MitDebug)
        return fail(QStringLiteral("当前 MIT 调试会话未连接"));
    errorText_.clear();
    if (gateway()) {
        canfd_.cancelPendingTransmission();
        emit logAppended(QStringLiteral("已停止当前电机的发送任务；此操作不等同于电机失能"));
    } else {
        emit legacyClearRequested(channel, id);
        if (!errorText_.isEmpty()) return false;
        emit logAppended(QStringLiteral("已将原 MIT CAN%1 槽位 %2 复位为原中性帧").arg(channel).arg(id));
    }
    emit stateChanged(); return true;
}
} // namespace Backend

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <deque>
#include <climits>
#include <cstring>
#include <memory>
#include <vector>

#include "SOEM_interface/SoemUtils.h"
#include "Backend/Flash/FlashService.h"

namespace {
struct FakeSlave {
    int opened = 0, closed = 0, requests = 0, bootExits = 0;
    int transferCalls = 0, dataPackets = 0;
    uint16 state = EC_STATE_PRE_OP;
    uint32 peerError = 0;
    bool timeout = false, mailboxFailure = false, invalidMailbox = false;
    bool preopFailure = false, bootError = false, exitFailure = false;
    bool packetMismatch = false;
    bool ecat2Canfd = false;
    int busyWrq = 0, busyData = 0, busyEof = 0;
    bool finalAck = false;
    uint8 lastOpcode = 0;
    uint32 lastPacket = 0;
    QByteArray lastPayload, acknowledgedBytes;
    struct Frame { uint8 opcode; uint32 packet; QByteArray payload; };
    std::vector<Frame> sentFrames;
    std::deque<ec_errort> errors;
} wire;
struct MailboxBuffer { ec_mbxbuft bytes{}; };
std::vector<std::unique_ptr<MailboxBuffer>> buffers;

ec_mbxbuft* mailbox()
{
    auto buffer = std::make_unique<MailboxBuffer>();
    auto* result = &buffer->bytes;
    buffers.push_back(std::move(buffer));
    return result;
}
uint32 packetValue(const ec_mbxbuft* buffer)
{
    uint32 value;
    std::memcpy(&value, *buffer + 8, sizeof(value));
    return etohl(value);
}
void response(ec_mbxbuft* buffer, uint8 opcode, uint32 value)
{
    auto* header = reinterpret_cast<ec_mbxheadert*>(buffer);
    header->length = htoes(6);
    header->mbxtype = ECT_MBXT_FOE;
    (*buffer)[6] = opcode;
    value = htoel(value);
    std::memcpy(*buffer + 8, &value, sizeof(value));
}
}

// Stub only hardware/transport boundaries. ecx_FOEwrite itself is production C.
extern "C" {
int ecx_init(ecx_contextt*, const char*) { ++wire.opened; return 1; }
void ecx_close(ecx_contextt*) { ++wire.closed; }
int ecx_config_init(ecx_contextt* context)
{
    context->slavecount = 1;
    context->slavelist[1].configadr = EC_NODEOFFSET + 1;
    context->slavelist[1].mbx_l = context->slavelist[1].mbx_rl = 512;
    context->slavelist[1].state = wire.state;
    if (wire.ecat2Canfd) {
        context->slavelist[1].eep_man = 0;
        context->slavelist[1].eep_id = 0xAB125;
        context->slavelist[1].eep_rev = 3;
    }
    return 1;
}
int ecx_writestate(ecx_contextt* context, uint16 slave)
{
    ++wire.requests;
    const auto wanted = context->slavelist[slave].state;
    if (wire.state == EC_STATE_BOOT && wanted == EC_STATE_INIT) {
        ++wire.bootExits;
        if (wire.exitFailure) return 0;
    }
    wire.state = wanted;
    return 1;
}
uint16 ecx_statecheck(ecx_contextt* context, uint16 slave, uint16 wanted, int)
{
    uint16 state = wire.state;
    if (slave == 0) state = wire.preopFailure ? EC_STATE_INIT : EC_STATE_PRE_OP;
    if (wire.bootError && wanted == EC_STATE_BOOT) state = EC_STATE_BOOT | EC_STATE_ERROR;
    context->slavelist[slave].state = state;
    context->slavelist[slave].ALstatuscode = wire.bootError ? 0x0015 : 0;
    return state & 0xf; // SOEM masks the ERROR bit in its return value.
}
uint32 ecx_readeeprom(ecx_contextt*, uint16, uint16 address, int)
{
    if (wire.invalidMailbox) return 0;
    return (512u << 16) | (address == ECT_SII_BOOTRXMBX ? 0x1000u : 0x1200u);
}
int ecx_FPWR(ecx_portt*, uint16, uint16, uint16, void*, int) { return wire.mailboxFailure ? 0 : 1; }
int ecx_FPRD(ecx_portt*, uint16, uint16, uint16 size, void* data, int)
{
    std::memset(data, 0, size);
    if (size == sizeof(ec_alstatust)) {
        auto* state = static_cast<ec_alstatust*>(data);
        state->alstatus = htoes(wire.state);
        state->alstatuscode = htoes(wire.bootError ? 0x0015 : 0);
    }
    return 1;
}
ec_mbxbuft* ecx_getmbx(ecx_contextt*) { return mailbox(); }
int ecx_dropmbx(ecx_contextt*, ec_mbxbuft*) { return 1; }
void ec_clearmbx(ec_mbxbuft* buffer) { std::memset(buffer, 0, sizeof(*buffer)); }
uint8 ec_nextmbxcnt(uint8 count) { return count >= 7 ? 1 : count + 1; }
int ecx_mbxsend(ecx_contextt*, uint16, ec_mbxbuft* buffer, int)
{
    wire.lastOpcode = (*buffer)[6];
    wire.lastPacket = packetValue(buffer);
    const auto size = etohs(reinterpret_cast<ec_mbxheadert*>(buffer)->length);
    wire.lastPayload = QByteArray(reinterpret_cast<const char*>(*buffer + 12), size - 6);
    wire.sentFrames.push_back({wire.lastOpcode, wire.lastPacket, wire.lastPayload});
    if (wire.lastOpcode == ECT_FOE_WRITE) ++wire.transferCalls;
    if (wire.lastOpcode == ECT_FOE_DATA) ++wire.dataPackets;
    return 1;
}
int ecx_mbxreceive(ecx_contextt*, uint16, ec_mbxbuft** buffer, int timeout)
{
    if (!timeout || wire.timeout) return 0;
    *buffer = mailbox();
    int* busy = wire.lastOpcode == ECT_FOE_WRITE ? &wire.busyWrq
        : wire.lastPayload.isEmpty() ? &wire.busyEof : &wire.busyData;
    if (*busy > 0) {
        if (*busy > 0) --*busy;
        response(*buffer, ECT_FOE_BUSY, 0);
        return 1;
    }
    if (!wire.peerError && !wire.packetMismatch && wire.lastOpcode == ECT_FOE_DATA) {
        wire.acknowledgedBytes += wire.lastPayload;
        if (wire.lastPayload.size() < 500) wire.finalAck = true;
    }
    response(*buffer, wire.peerError ? ECT_FOE_ERROR : ECT_FOE_ACK,
             wire.peerError ? wire.peerError : wire.lastOpcode == ECT_FOE_WRITE ? 0
                 : wire.lastPacket + (wire.packetMismatch ? 1 : 0));
    return 1;
}
void ecx_pusherror(ecx_contextt*, const ec_errort* error) { wire.errors.push_back(*error); }
boolean ecx_poperror(ecx_contextt*, ec_errort* error)
{
    if (wire.errors.empty()) return FALSE;
    *error = wire.errors.front(); wire.errors.pop_front(); return TRUE;
}
// EEPROM helpers are linked in the same utility translation unit, never called.
ec_adaptert* ec_find_adapters() { return nullptr; }
void ec_free_adapters(ec_adaptert*) {}
int ecx_BRD(ecx_portt*, uint16, uint16, uint16, void*, int) { return 0; }
int ecx_APRD(ecx_portt*, uint16, uint16, uint16, void*, int) { return 0; }
int ecx_APWR(ecx_portt*, uint16, uint16, uint16, void*, int) { return 0; }
uint64 ecx_readeepromAP(ecx_contextt*, uint16, uint16, int) { return 0; }
int ecx_writeeepromAP(ecx_contextt*, uint16, uint16, uint16, int) { return 0; }
}

class FirmwareTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString file;
    QByteArray firmwareBytes;
private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        file = directory.filePath(QStringLiteral("firmware.bin"));
        QFile firmware(file);
        QVERIFY(firmware.open(QIODevice::WriteOnly));
        firmwareBytes.resize(1001);
        for (int i = 0; i < firmwareBytes.size(); ++i)
            firmwareBytes[i] = char((i * 37 + i / 13) & 0xff);
        QCOMPARE(firmware.write(firmwareBytes), qint64(1001));
    }
    void init() { wire = {}; buffers.clear(); }
    void failedDownloadDoesNotRequestTheResetTransition()
    {
        wire.peerError = 0x8001;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(wire.bootExits, 0);
        QCOMPARE(wire.state, uint16(EC_STATE_BOOT));
    }
    void peerErrorsKeepBothCodeLayers_data()
    {
        QTest::addColumn<quint32>("peerCode");
        QTest::addColumn<int>("soemCode");
        QTest::newRow("filename") << quint32(0x8001) << -10;
        QTest::newRow("package-or-flash") << quint32(0x8000) << -5;
        QTest::newRow("access") << quint32(0x8002) << -5;
        QTest::newRow("not-in-boot") << quint32(0x8009) << -5;
        QTest::newRow("unknown") << quint32(0xdeadbeef) << -5;
    }
    void peerErrorsKeepBothCodeLayers()
    {
        QFETCH(quint32, peerCode);
        QFETCH(int, soemCode);
        wire.peerError = peerCode;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        tool->close();
        const auto& result = tool->lastResult();
        QCOMPARE(result.error, soem_interface::FirmwareFlashError::TransferFailed);
        QCOMPARE(result.soemReturn, soemCode);
        QCOMPARE(result.foeError, peerCode);
        QVERIFY(!result.downloadComplete);
        QVERIFY(!result.activationRequested);
        QVERIFY(result.alStateValid);
        QCOMPARE(result.alState, uint16_t(EC_STATE_BOOT));
        const auto message = QString::fromStdString(result.message);
        QVERIFY(message.contains(QStringLiteral("FW010")));
        QVERIFY(message.contains(QString::number(soemCode)));
        QVERIFY(message.contains(QString::number(peerCode, 16), Qt::CaseInsensitive));
        QCOMPARE(wire.bootExits, 0);
    }
    void timeoutHasNoInventedPeerError()
    {
        wire.timeout = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(tool->lastResult().soemReturn, 0);
        QCOMPARE(tool->lastResult().foeError, uint32_t(0));
        QVERIFY(QString::fromStdString(tool->lastResult().message).contains(QStringLiteral("超时")));
        QCOMPARE(wire.bootExits, 0);
    }
    void unexpectedPacketNumberPreservesBoot()
    {
        wire.packetMismatch = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(tool->lastResult().soemReturn, -7);
        QCOMPARE(wire.bootExits, 0);
    }
    void foeReadAlsoPreservesThePeerError()
    {
        wire.peerError = 0x800c;
        auto context = std::make_unique<ecx_contextt>();
        context->slavelist[1].mbx_l = context->slavelist[1].mbx_rl = 512;
        char name[] = "firmware.bin", data[16]{};
        int length = sizeof(data);
        QCOMPARE(ecx_FOEread(context.get(), 1, name, 0, &length, data, EC_TIMEOUTRXM), -5);
        ec_errort error{};
        QVERIFY(ecx_poperror(context.get(), &error));
        QCOMPARE(error.Etype, EC_ERR_TYPE_FOE_ERROR);
        QCOMPARE(error.AbortCode, int32(0x800c));
    }
    void missingFileIsRejectedBeforeOpeningTheNic()
    {
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, directory.filePath("missing.bin").toStdString()));
        QCOMPARE(wire.opened, 0);
    }
    void rawEcat2CanfdImageIsRejectedBeforeOpeningTheNic()
    {
        // Reproduces the supplied legacy M7 BIN's vector table, not a container.
        const auto path = directory.filePath("ecat2canfd.bin");
        QFile raw(path);
        QVERIFY(raw.open(QIODevice::WriteOnly));
        raw.write(QByteArray::fromHex("00000824e94e0208"));
        raw.write(QByteArray(500, '\0'));
        raw.close();
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, path.toStdString()));
        QCOMPARE(wire.opened, 0);
        QVERIFY(QString::fromStdString(tool->lastResult().message).contains("FW012"));
        QVERIFY(QString::fromStdString(tool->lastResult().message).contains("EC2PKG01"));
    }
    void ecat2CanfdContainerIsAcceptedButTruncationIsRejected_data()
    {
        QTest::addColumn<bool>("truncated");
        QTest::newRow("valid container") << false;
        QTest::newRow("truncated container") << true;
    }
    void ecat2CanfdContainerIsAcceptedButTruncationIsRejected()
    {
        QFETCH(bool, truncated);
        // Synthetic non-executable core images, for host validation tests only.
        QFile fixture(QFINDTESTDATA("fixtures/ecat2canfd-v1.bin"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        auto bytes = fixture.readAll();
        if (truncated) bytes.chop(1);
        const auto path = directory.filePath("ecat2canfd.bin");
        QFile firmware(path);
        QVERIFY(firmware.open(QIODevice::WriteOnly));
        QCOMPARE(firmware.write(bytes), qint64(bytes.size()));
        firmware.close();
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QCOMPARE(tool->flashFirmware(1, path.toStdString()), !truncated);
        if (truncated) {
            QCOMPARE(wire.opened, 0);
            QVERIFY(QString::fromStdString(tool->lastResult().message).contains("FW012"));
        } else {
            QCOMPARE(wire.bootExits, 1);
        }
    }
    void identifiedEcat2CanfdRejectsWrongFilenameBeforeBoot()
    {
        wire.ecat2Canfd = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(tool->lastResult().error, soem_interface::FirmwareFlashError::InvalidPackage);
        QCOMPARE(wire.requests, 0);
        QCOMPARE(wire.transferCalls, 0);
    }
    void emptyFileIsRejectedBeforeOpeningTheNic()
    {
        const auto empty = directory.filePath("empty.bin");
        QFile firmware(empty);
        QVERIFY(firmware.open(QIODevice::WriteOnly));
        firmware.close();
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, empty.toStdString()));
        QCOMPARE(tool->lastResult().error, soem_interface::FirmwareFlashError::InvalidFile);
        QCOMPARE(wire.opened, 0);
    }
    void invalidBootMailboxPreventsTransfer()
    {
        wire.invalidMailbox = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(tool->lastResult().error, soem_interface::FirmwareFlashError::InvalidBootMailbox);
        QCOMPARE(wire.transferCalls, 0);
    }
    void mailboxWriteFailurePreventsTransfer()
    {
        wire.mailboxFailure = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(tool->lastResult().error, soem_interface::FirmwareFlashError::BootMailboxWriteFailed);
        QCOMPARE(wire.transferCalls, 0);
    }
    void bootErrorFlagIsNotAcceptedAsBoot()
    {
        wire.bootError = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(tool->lastResult().error, soem_interface::FirmwareFlashError::BootFailed);
        QCOMPARE(tool->lastResult().alStatusCode, uint16_t(0x0015));
        QCOMPARE(wire.transferCalls, 0);
    }
    void preopFailureIsChecked()
    {
        wire.preopFailure = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->init());
    }
    void successfulTransferSendsEveryPacketAndRequestsActivation()
    {
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(wire.dataPackets, 3);
        QCOMPARE(wire.bootExits, 1);
        QVERIFY(tool->lastResult().downloadComplete);
        QVERIFY(tool->lastResult().activationRequested);
    }
    void initialBusyDoesNotClaimSuccessBeforeAnyDataWasAcknowledged()
    {
        wire.busyWrq = 1;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(tool->flashFirmware(1, file.toStdString()));
        QVERIFY(wire.finalAck);
        QCOMPARE(wire.acknowledgedBytes, firmwareBytes);
        QCOMPARE(wire.sentFrames[0].opcode, uint8(ECT_FOE_WRITE));
        QCOMPARE(wire.sentFrames[1].opcode, uint8(ECT_FOE_WRITE));
        QCOMPARE(wire.sentFrames[1].packet, wire.sentFrames[0].packet);
        QCOMPARE(wire.sentFrames[1].payload, wire.sentFrames[0].payload);
    }
    void busyOnZeroLengthEofRequiresItsFinalAck()
    {
        const auto path = directory.filePath("exact-mailbox-multiple.bin");
        QFile firmware(path);
        QVERIFY(firmware.open(QIODevice::WriteOnly));
        const QByteArray payload(1000, char(0x37));
        QCOMPARE(firmware.write(payload), qint64(payload.size()));
        firmware.close();
        wire.busyEof = 1;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(tool->flashFirmware(1, path.toStdString()));
        QVERIFY(wire.finalAck);
        QCOMPARE(wire.acknowledgedBytes, payload);
        QCOMPARE(wire.dataPackets, 4); // Two full packets, EOF, identical EOF retry.
        QCOMPARE(wire.sentFrames[3].packet, wire.sentFrames[4].packet);
        QVERIFY(wire.sentFrames[4].payload.isEmpty());
    }
    void busyRetriesKeepTheDataAndPacketNumber()
    {
        wire.busyData = 3;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(tool->flashFirmware(1, file.toStdString()));
        QVERIFY(wire.finalAck);
        QCOMPARE(wire.acknowledgedBytes, firmwareBytes);
        for (size_t retry = 2; retry <= 4; ++retry) {
            QCOMPARE(wire.sentFrames[retry].packet, wire.sentFrames[1].packet);
            QCOMPARE(wire.sentFrames[retry].payload, wire.sentFrames[1].payload);
        }
    }
    void persistentBusyTimesOutWithoutRequestingActivation_data()
    {
        QTest::addColumn<int>("stage");
        QTest::newRow("write-request") << 0;
        QTest::newRow("data") << 1;
        QTest::newRow("zero-length-eof") << 2;
    }
    void persistentBusyTimesOutWithoutRequestingActivation()
    {
        QFETCH(int, stage);
        if (stage == 0) wire.busyWrq = INT_MAX;
        if (stage == 1) wire.busyData = INT_MAX;
        QString path = file;
        if (stage == 2) {
            wire.busyEof = INT_MAX;
            path = directory.filePath("busy-eof.bin");
            QFile firmware(path);
            QVERIFY(firmware.open(QIODevice::WriteOnly));
            QCOMPARE(firmware.write(QByteArray(1000, char(0x37))), qint64(1000));
        }
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, path.toStdString()));
        QCOMPARE(tool->lastResult().soemReturn, 0);
        QVERIFY(!tool->lastResult().downloadComplete);
        QCOMPARE(wire.bootExits, 0);
        QVERIFY(!wire.finalAck);
    }
    void activationWriteFailureIsSeparateFromDownloadFailure()
    {
        wire.exitFailure = true;
        auto tool = std::make_unique<soem_interface::FirmwareTool>("fake");
        QVERIFY(!tool->flashFirmware(1, file.toStdString()));
        QCOMPARE(tool->lastResult().error, soem_interface::FirmwareFlashError::ActivationFailed);
        QVERIFY(tool->lastResult().downloadComplete);
        QVERIFY(!tool->lastResult().activationRequested);
    }
    void servicePropagatesCodesWithoutFalseCompletionProgress()
    {
        wire.peerError = 0x8001;
        Backend::FlashService service;
        QSignalSpy errors(&service, &Backend::FlashService::errorOccurred);
        QSignalSpy finished(&service, &Backend::FlashService::flashFinished);
        QSignalSpy progress(&service, &Backend::FlashService::flashProgress);
        QVERIFY(service.flashFirmware("fake", 1, file));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        QCOMPARE(errors.size(), 1);
        QVERIFY(!finished[0][1].toBool());
        for (const auto& message : {errors[0][0].toString(), finished[0][2].toString()}) {
            QVERIFY(message.contains("FW010"));
            QVERIFY(message.contains("SOEM 返回值：-10"));
            QVERIFY(message.contains("FoE 错误码：0x00008001"));
        }
        for (const auto& update : progress) QVERIFY(update[1].toInt() < 100);
        QCOMPARE(wire.bootExits, 0);
    }
    void serviceValidatesFileBeforeInitializingTheBus()
    {
        Backend::FlashService service;
        QSignalSpy finished(&service, &Backend::FlashService::flashFinished);
        QVERIFY(service.flashFirmware("fake", 1, directory.filePath("missing.bin")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        QVERIFY(!finished[0][1].toBool());
        QVERIFY(finished[0][2].toString().contains("FW001"));
        QCOMPARE(wire.opened, 0);
    }
};
QTEST_GUILESS_MAIN(FirmwareTests)
#include "FirmwareTests.moc"

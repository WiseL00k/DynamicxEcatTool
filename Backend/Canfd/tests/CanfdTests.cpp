#include <QtTest>
#include "Backend/Canfd/Ecat2CanfdController.h"
#include "Backend/Ethercat/BusSessionCoordinator.h"
#include <cstring>
#include <mutex>
#include <algorithm>
#include <stdexcept>

using namespace Backend;
using namespace soem_interface;

namespace {
BusScanResult scanFixture()
{
    BusScanResult scan;
    scan.success = scan.mappingReady = scan.activePdoComplete = true;
    scan.slaveCount = 1; scan.expectedWorkingCounter = 3;
    SlaveIdentitySnapshot s;
    s.position = 1; s.name = "ECAT2CANFD"; s.productCode = 0xAB125; s.revision = 3;
    s.outputBits = s.inputBits = 1480; s.state = EC_STATE_PRE_OP;
    scan.slaves.push_back(s);
    for (auto direction : {PdoDirection::Rx, PdoDirection::Tx}) {
        uint32_t offset = 0;
        auto add = [&](uint16_t index, uint8_t sub, uint16_t bits) {
            ActivePdoEntry e; e.slave = 1; e.direction = direction; e.index = index;
            e.subindex = sub; e.bitLength = bits; e.bitOffset = offset;
            scan.activePdos.push_back(e); offset += bits;
        };
        if (direction == PdoDirection::Rx) add(0x6040, 0, 32);
        const quint16 indexesRx[]{0x7000, 0x7001, 0x7002, 0x7003};
        const quint16 indexesTx[]{0x6000, 0x6001, 0x6002, 0x6003};
        for (int field = 0; field < 4; ++field) for (int slot = 1; slot <= 9; ++slot)
            add(direction == PdoDirection::Rx ? indexesRx[field] : indexesTx[field], quint8(slot), field < 2 ? 16 : 64);
        if (direction == PdoDirection::Rx) add(0x7005, 0, 8);
        else { add(0x600C, 0, 8); add(0x6041, 0, 32); }
    }
    return scan;
}

class FakeSession final : public CanfdSession {
public:
    struct Write { quint16 index; quint8 sub; std::vector<quint8> bytes; };
    BusScanResult scan{scanFixture()};
    std::vector<Write> writes;
    quint32 values[2][9]{{8,1,0,1000000,5000000,5,0,0,0},{8,1,0,1000000,5000000,5,0,0,0}};
    int failWriteSub{-1};
    int failReadSub{-1};
    bool returnShort{false};
    bool mismatchApply{false};
    std::string cleanupWarningOnStop;
    std::string disconnectError;
    int stops{0}, disconnects{0};
    PrepareOutputs prepare;
    ConsumeInputs consume;
    mutable std::mutex mutex;
    DcTestSnapshot snap;
    BusScanResult connectBus(const std::string&) override { return scan; }
    void disconnectBus() override {
        std::lock_guard<std::mutex> lock(mutex); ++disconnects; snap.operational = false;
        if (!disconnectError.empty()) throw std::runtime_error(disconnectError);
    }
    DcStartResult start(const ApplicationRunOptions& options, PrepareOutputs p, ConsumeInputs c) override {
        std::lock_guard<std::mutex> lock(mutex);
        prepare = std::move(p); consume = std::move(c);
        snap = {}; snap.operational = true; snap.expectedWkc = snap.wkc = 3; snap.configured = options.dcEnabled;
        return {true, false, {}};
    }
    void requestStop() override {}
    void stop() override { std::lock_guard<std::mutex> lock(mutex); ++stops; snap.operational = false; snap.finished = true; snap.cleanupWarning = cleanupWarningOnStop; }
    DcTestSnapshot snapshot() const override { std::lock_guard<std::mutex> lock(mutex); return snap; }
    SdoTransferResult readSdo(int, quint16 index, quint8 sub, int size, void* data) override {
        SdoTransferResult r; r.actualSize = size; r.wkc = 1;
        if (index == 0x1018) {
            quint32 identity[]{4,0,0xAB125,3};
            const auto bytes = canfd::writeLe(identity[sub], size); std::memcpy(data, bytes.data(), size); r.success = true; return r;
        }
        if (sub == failReadSub) { r.abortCode = 0x06020000; r.error = "missing object"; return r; }
        const auto bytes = canfd::writeLe(values[index - 0x2000][sub], size);
        std::memcpy(data, bytes.data(), size); r.success = true;
        if (returnShort) r.actualSize = size - 1;
        return r;
    }
    SdoTransferResult writeSdo(int, quint16 index, quint8 sub, const std::vector<quint8>& bytes) override {
        writes.push_back({index,sub,bytes});
        SdoTransferResult r; r.actualSize = int(bytes.size()); r.wkc = 1;
        if (sub == failWriteSub) { r.abortCode = 0x06090030; r.error = "value exceeded"; return r; }
        r.success = true;
        auto& channel = values[index - 0x2000];
        channel[sub] = canfd::readLe(bytes.data(), int(bytes.size()));
        if (sub < 6) channel[7] = 1;
        else { channel[6] = 0; channel[7] = 0; if (mismatchApply) channel[3] = 125000; }
        return r;
    }
};

QVariantMap configMap(int channel = 0)
{
    return {{"channel", channel}, {"enabled", true}, {"format", 2}, {"nominalBitrate", 500000}, {"dataBitrate", 2500000}, {"options", 7}};
}

SlaveProcessImage image()
{ SlaveProcessImage i; i.position = 1; i.outputs.resize(185); i.inputs.resize(185); return i; }
}

class CanfdTests : public QObject {
    Q_OBJECT
private slots:
    void wireCodecAndBoundaries()
    {
        canfd::Frame f; QString error;
        QVERIFY(canfd::parseFrame(1, 0, "0x123", 8, "01 23 45 67 89 AB CD EF", f, error));
        std::vector<uint8_t> output(185);
        canfd::encodeFrame(output, 0, f);
        QCOMPARE(output[4], quint8(0x23)); QCOMPARE(output[5], quint8(0x89));
        QCOMPARE(output[40], quint8(0x01)); QCOMPARE(output[47], quint8(0xEF));
        QCOMPARE(output[184], quint8(0));
        QVERIFY(!canfd::parseFrame(1, 0, "800", 0, "", f, error));
        QVERIFY(!canfd::parseFrame(1, 0, "123", 9, "", f, error));
        QVERIFY(!canfd::parseFrame(1, 2, "123", 0, "", f, error));
        QVERIFY(!canfd::parseFrame(1, 0, "123", 2, "0A", f, error));
        QVERIFY(!canfd::parseFrame(1, 0, "123", 1, "GG", f, error));
        QVERIFY(canfd::parseFrame(1, 1, "7FF", 0, "", f, error));
        canfd::encodeFrame(output, 8, f);
        QCOMPARE(output[38], quint8(0xff)); QCOMPARE(output[39], quint8(0x0f));
        QCOMPARE(canfd::decodeRows(output, true).size(), 18);
    }

    void identityAndMappingValidation()
    {
        auto scan = scanFixture(); QString error;
        QVERIFY(canfd::validateScan(scan, 1, error));
        QVERIFY(!canfd::validateScan(scan, 2, error));
        scan.activePdos[4].bitOffset += 8;
        QVERIFY(!canfd::validateScan(scan, 1, error));
        scan = scanFixture(); scan.slaves[0].inputBits = 1472;
        QVERIFY(!canfd::validateScan(scan, 1, error));
        scan = scanFixture(); scan.slaves[0].productCode++;
        QVERIFY(!canfd::validateScan(scan, 1, error));
    }

    void configurationRequiresApplyAndReadback()
    {
        FakeSession session; std::atomic<bool> cancel{false};
        canfd::ChannelConfig c; QString error;
        QVERIFY(canfd::parseConfig(1, configMap(), c, error));
        const auto result = applyCanfdConfiguration(session, {c}, cancel);
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(session.writes.size(), size_t(6));
        const size_t sizes[]{1,1,4,4,2,1};
        for (size_t i = 0; i < 6; ++i) { QCOMPARE(session.writes[i].sub, quint8(i+1)); QCOMPARE(session.writes[i].bytes.size(), sizes[i]); }
        QCOMPARE(result.configs[0].status, quint8(0));
        QCOMPARE(result.configs[0].nominalBitrate, quint32(500000));
        session.writes.clear(); session.failWriteSub = 3;
        auto failed = applyCanfdConfiguration(session, {c}, cancel);
        QVERIFY(!failed.success); QCOMPARE(session.writes.size(), size_t(3));
        QVERIFY(failed.error.contains("06090030"));
        session.failWriteSub = -1; session.mismatchApply = true;
        QVERIFY(!applyCanfdConfiguration(session, {c}, cancel).success);
        session.returnShort = true;
        QVERIFY(!readCanfdConfiguration(session, {1}, cancel).success);
    }

    void receiveModelIsBounded()
    {
        CanFrameModel model;
        std::deque<CanFrameRecord> records(10003);
        for (int i = 0; i < 10003; ++i) { records[i].timestampUs = i; records[i].frame.id = i & 0x7ff; }
        QCOMPARE(model.append(std::move(records)), quint64(3));
        QCOMPARE(model.rowCount(), 10000);
        QCOMPARE(model.data(model.index(0), CanFrameModel::IdHexRole).toString(), QString("003"));
        model.clear(); QCOMPARE(model.rowCount(), 0);
    }

    void controllerSingleSubmissionAndReceiveSemantics()
    {
        BusSessionCoordinator coordinator;
        auto fake = std::make_unique<FakeSession>(); auto* session = fake.get();
        Ecat2CanfdController controller(coordinator, std::move(fake));
        controller.setNicName("fake"); controller.connectBus(1);
        QTRY_VERIFY(controller.connected() && !controller.busy());
        QVERIFY(controller.canStart()); QCOMPARE(controller.channelConfigs().size(), 2);
        controller.start(100001, false, 0); QVERIFY(!controller.running());
        controller.start(2000, false, 0); QTRY_VERIFY(controller.running());
        controller.sendOnce(1,0,"123",1,"A5");
        std::vector<SlaveProcessImage> images{image()}; session->prepare(images);
        QCOMPARE(images[0].outputs[4], quint8(0x23)); QCOMPARE(images[0].outputs[5], quint8(0x19));
        // A failed WKC cannot cause the next prepare call to replay a command.
        ApplicationCycle cycle; cycle.sequence = 1; cycle.workingCounter = 0; cycle.expectedWorkingCounter = 3; cycle.slaves = images;
        session->consume(cycle); session->prepare(images);
        QVERIFY(std::all_of(images[0].outputs.begin(),images[0].outputs.end(),[](auto b){return b==0;}));
        cycle.slaves[0].inputs[0] = 0x23; cycle.slaves[0].inputs[1] = 0x19; cycle.slaves[0].inputs[36] = 0xA5;
        cycle.sequence = 2; cycle.workingCounter = 3; session->consume(cycle); session->consume(cycle);
        cycle.sequence = 3; session->consume(cycle);
        QTRY_COMPARE(controller.frames()->rowCount(), 2); // equal real observations retained; same host sample ignored
        cycle.sequence = 4; cycle.slaves[0].inputs[1] = 0x11; session->consume(cycle);
        QTest::qWait(50); QCOMPARE(controller.frames()->rowCount(), 2);
        QCOMPARE(controller.pdoSnapshot().value("txRows").toList().size(), 18);
        controller.startPeriodic(1,0,"123",1,"A5",3001);
        QVERIFY(controller.periodicSending()); QCOMPARE(controller.periodicIntervalUs(),4000);
        cycle.sequence = 5; cycle.workingCounter = 0; session->consume(cycle);
        QTRY_VERIFY(!controller.periodicSending());
        cycle.sequence = 6; cycle.workingCounter = 3; session->consume(cycle);
        QVERIFY(!controller.periodicSending());
        controller.startPeriodic(1,0,"123",1,"A5",3001);
        controller.applyConfiguration(1, {configMap()});
        QVERIFY(!controller.periodicSending());
        session->prepare(images); cycle.sequence = 7; cycle.slaves = images; session->consume(cycle);
        QTRY_VERIFY(!controller.busy()); QVERIFY(controller.running());
        controller.stop(); QTRY_COMPARE(controller.state(),QString("preop")); QVERIFY(controller.connected());
        QCOMPARE(controller.pdoSnapshot().value("wkc").toInt(), 3);
        QCOMPARE(controller.pdoSnapshot().value("expectedWkc").toInt(), 3);
        QVERIFY(controller.pdoSnapshot().value("historical").toBool());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
        QCOMPARE(coordinator.mode(), BusSessionCoordinator::Mode::Idle);
    }

    void countMismatchKeepsPreopAndBlocksWrites()
    {
        BusSessionCoordinator coordinator;
        auto fake = std::make_unique<FakeSession>(); auto* session = fake.get();
        Ecat2CanfdController controller(coordinator, std::move(fake));
        controller.setNicName("fake"); controller.connectBus(2);
        QTRY_VERIFY(controller.connected() && !controller.busy());
        QVERIFY(!controller.canStart()); QVERIFY(!controller.errorText().isEmpty());
        controller.applyConfiguration(1,{configMap()}); QVERIFY(session->writes.empty());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
    }

    void unconfirmedCleanupBlocksRestart()
    {
        BusSessionCoordinator coordinator;
        auto fake = std::make_unique<FakeSession>(); auto* session = fake.get();
        session->cleanupWarningOnStop = "PRE-OP cleanup was not confirmed.";
        Ecat2CanfdController controller(coordinator, std::move(fake));
        controller.setNicName("fake"); controller.connectBus(1);
        QTRY_VERIFY(controller.canStart());
        controller.start(2000, true, 0); QTRY_VERIFY(controller.running());
        controller.stop(); QTRY_COMPARE(controller.state(), QString("faulted"));
        QVERIFY(controller.connected()); QVERIFY(!controller.canStart()); QVERIFY(!controller.busy());
        QVERIFY(!controller.diagnostics().value("cleanupWarning").toString().isEmpty());
        controller.readConfiguration(1); QTRY_VERIFY(!controller.busy());
        QVERIFY(!controller.canStart());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
        QCOMPARE(coordinator.mode(), BusSessionCoordinator::Mode::Idle);
    }

    void disconnectReportsCleanupFailureAfterRelease()
    {
        BusSessionCoordinator coordinator;
        auto fake = std::make_unique<FakeSession>(); auto* session = fake.get();
        session->disconnectError = "SYNC0 shutdown was not verified for slave 1.";
        Ecat2CanfdController controller(coordinator, std::move(fake));
        QSignalSpy released(&controller, &Ecat2CanfdController::sessionReleased);
        QSignalSpy errors(&controller, &Ecat2CanfdController::errorOccurred);
        controller.setNicName("fake"); controller.connectBus(1);
        QTRY_VERIFY(controller.canStart());
        controller.start(2000, true, 0); QTRY_VERIFY(controller.running());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
        QCOMPARE(controller.state(), QString("failed"));
        QVERIFY(!controller.connected()); QVERIFY(!controller.busy()); QVERIFY(!controller.canStart());
        QVERIFY(controller.errorText().contains("SYNC0 shutdown"));
        QCOMPARE(released.count(), 1); QCOMPARE(errors.count(), 1); QCOMPARE(session->disconnects, 1);
        QCOMPARE(coordinator.mode(), BusSessionCoordinator::Mode::Idle);
    }
};

QTEST_GUILESS_MAIN(CanfdTests)
#include "CanfdTests.moc"

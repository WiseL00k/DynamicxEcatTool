#include <QtTest>

#include "Backend/Commands/DamiaoMitController.h"
#include "Backend/Ethercat/EthercatMasterController.h"
#include "Backend/Ethercat/EthercatBackend.h"
#include "DamiaoTestSession.h"

#include <algorithm>
#include <limits>

using namespace Backend;

namespace {
QVariantMap controlValues()
{
    return {{"pos", 0.0}, {"vel", 0.0}, {"torque", 0.0}, {"kp", 0.0},
            {"kd", 0.0}, {"PMAX", 12.5}, {"VMAX", 30.0}, {"TMAX", 10.0}};
}

QVariantList outgoingFrames(const std::vector<soem_interface::SlaveProcessImage>& images)
{
    QVariantList frames;
    for (const auto& image : images)
        for (const auto& row : canfd::decodeRows(image.outputs, true))
            if (row.toMap().value("valid").toBool()) frames.push_back(row);
    return frames;
}

QVariantList advanceSingleSubmission(damiao_test::Session& session)
{
    QVariantList frames;
    // A command may have a separating empty cycle. Verify the wire result and
    // lack of replay without coupling ordinary sends to that internal delay.
    for (int cycle = 0; cycle < 3; ++cycle)
        frames.append(outgoingFrames(session.cycle()));
    return frames;
}
}

class DamiaoMitTests : public QObject {
    Q_OBJECT
private slots:
    void legacyConnectionWithoutNicReportsOnceAndReleasesBusyState()
    {
        EthercatBackend backend;
        auto* controller = backend.damiaoMit();
        QSignalSpy errors(&backend, &EthercatBackend::soemErrorOccurred);
        controller->connectBus(); // Real signal wiring; no NIC selected or socket opened.
        QTRY_VERIFY(!controller->busy());
        QVERIFY(!controller->active()); QVERIFY(!controller->connected());
        QVERIFY(!backend.sessionActive()); QVERIFY(!backend.connected());
        QVERIFY(!controller->errorText().isEmpty());
        QCOMPARE(errors.count(), 1);
    }

    void legacyPreflightRejectsCountTypeAndImageMismatch()
    {
        const auto valid = damiao_test::scanFixture(false);
        QVERIFY(validateMitDebugScan(valid).ok);
        auto scan = valid;
        scan.slaveCount = 2; scan.slaves.push_back(scan.slaves.front());
        QVERIFY(!validateMitDebugScan(scan).ok);
        QVERIFY(!validateMitDebugScan(damiao_test::scanFixture(true)).ok);
        scan = valid; scan.slaves[0].position = 2;
        QVERIFY(!validateMitDebugScan(scan).ok);
        scan = valid; scan.slaves[0].inputBits -= 8;
        QVERIFY(!validateMitDebugScan(scan).ok);
        scan = valid; scan.mappingReady = false;
        QVERIFY(!validateMitDebugScan(scan).ok);
        scan = valid; scan.success = false;
        QVERIFY(!validateMitDebugScan(scan).ok);
    }

    void previewEncodesBoundariesWithoutConnecting()
    {
        BusSessionCoordinator owner;
        auto fake = std::make_unique<damiao_test::Session>();
        auto* session = fake.get();
        DamiaoMitController controller(owner, std::move(fake));
        auto values = controlValues();
        auto preview = controller.preview(values);
        QVERIFY(preview.value("valid").toBool());
        QCOMPARE(preview.value("hex").toString(), QString("7F FF 7F F0 00 00 07 FF"));
        QCOMPARE(preview.value("bytes").toList().size(), 8);
        values["pos"] = -12.5; values["vel"] = -30.0; values["torque"] = -10.0;
        QCOMPARE(controller.preview(values).value("hex").toString(), QString("00 00 00 00 00 00 00 00"));
        values["pos"] = 12.5; values["vel"] = 30.0; values["torque"] = 10.0;
        values["kp"] = 500.0; values["kd"] = 5.0;
        QCOMPARE(controller.preview(values).value("hex").toString(), QString("FF FF FF FF FF FF FF FF"));
        QCOMPARE(session->connects.load(), 0);
        QCOMPARE(session->starts.load(), 0);
        QCOMPARE(session->writes.load(), 0);
        QCOMPARE(owner.mode(), BusSessionCoordinator::Mode::Idle);
    }

    void invalidControlValues_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<double>("value");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        for (const char* key : {"pos", "vel", "torque", "kp", "kd", "PMAX", "VMAX", "TMAX"})
            QTest::newRow((QByteArray(key) + "-nan").constData()) << QString::fromLatin1(key) << nan;
        QTest::newRow("infinite-position") << QString("pos") << infinity;
        QTest::newRow("zero-position-range") << QString("PMAX") << 0.0;
        QTest::newRow("negative-velocity-range") << QString("VMAX") << -1.0;
        QTest::newRow("zero-torque-range") << QString("TMAX") << 0.0;
        QTest::newRow("position-overflow") << QString("pos") << 12.5001;
        QTest::newRow("velocity-underflow") << QString("vel") << -30.0001;
        QTest::newRow("torque-overflow") << QString("torque") << 10.0001;
        QTest::newRow("negative-kp") << QString("kp") << -0.001;
        QTest::newRow("kp-overflow") << QString("kp") << 500.001;
        QTest::newRow("kd-overflow") << QString("kd") << 5.001;
    }

    void previewNormalizesExtremeButFiniteProtocolRanges()
    {
        BusSessionCoordinator owner;
        DamiaoMitController controller(owner, std::make_unique<damiao_test::Session>());
        for (const double limit : {1e308, std::numeric_limits<double>::denorm_min()}) {
            auto values = controlValues();
            values["PMAX"] = values["VMAX"] = values["TMAX"] = limit;
            values["pos"] = values["torque"] = limit; values["vel"] = -limit;
            const auto preview = controller.preview(values);
            QVERIFY2(preview.value("valid").toBool(), qPrintable(preview.value("error").toString()));
            QCOMPARE(preview.value("hex").toString(), QString("FF FF 00 00 00 00 0F FF"));
        }
        auto values = controlValues(); values.remove("PMAX");
        QVERIFY(!controller.preview(values).value("valid").toBool());
    }

    void invalidControlValues()
    {
        QFETCH(QString, key); QFETCH(double, value);
        BusSessionCoordinator owner;
        DamiaoMitController controller(owner, std::make_unique<damiao_test::Session>());
        auto values = controlValues(); values[key] = value;
        const auto preview = controller.preview(values);
        QVERIFY(!preview.value("valid").toBool());
        QVERIFY(!preview.value("error").toString().isEmpty());
        QVERIFY(preview.value("bytes").toList().isEmpty());
    }

    void gatewayRejectsWrongTopologyBeforeOp_data()
    {
        QTest::addColumn<int>("defect");
        QTest::newRow("two-slaves") << 0;
        QTest::newRow("legacy-selected-as-gateway") << 1;
        QTest::newRow("shifted-mapping") << 2;
        QTest::newRow("sdo-identity-disagrees") << 3;
    }

    void gatewayRejectsWrongTopologyBeforeOp()
    {
        QFETCH(int, defect);
        BusSessionCoordinator owner;
        auto fake = std::make_unique<damiao_test::Session>(defect != 1);
        auto* session = fake.get();
        if (defect == 0) {
            session->scan.slaveCount = 2;
            auto second = session->scan.slaves.front(); second.position = 2;
            session->scan.slaves.push_back(second);
        }
        if (defect == 2) session->scan.activePdos[1].bitOffset += 8;
        if (defect == 3) session->identityMismatch = true;
        DamiaoMitController controller(owner, std::move(fake));
        QVERIFY(controller.setSlaveType("ecat2canfd"));
        controller.setNicName("fake"); controller.connectBus();
        QTRY_VERIFY(session->connects.load() == 1 && !controller.busy());
        QVERIFY(!controller.canStart());
        controller.start(2000, false, 0);
        QCOMPARE(session->starts.load(), 0);
        QCOMPARE(session->writes.load(), 0);
        QVERIFY(!controller.running());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
    }

    void sessionOwnershipCannotBeBorrowedFromGenericGateway()
    {
        BusSessionCoordinator owner;
        auto firstFake = std::make_unique<damiao_test::Session>();
        Ecat2CanfdController generic(owner, std::move(firstFake));
        generic.setNicName("fake"); generic.connectBus(1);
        QTRY_VERIFY(generic.connected() && !generic.busy());

        auto secondFake = std::make_unique<damiao_test::Session>();
        auto* session = secondFake.get();
        DamiaoMitController controller(owner, std::move(secondFake));
        QVERIFY(controller.setSlaveType("ecat2canfd"));
        controller.setNicName("fake"); controller.connectBus();
        QVERIFY(!controller.active());
        QCOMPARE(session->connects.load(), 0);
        QVERIFY(!controller.sendControl(0, "1", controlValues()));
        QVERIFY(!controller.enableMotor(0, "1"));
        QCOMPARE(session->starts.load(), 0);
        generic.disconnectBus(); QTRY_VERIFY(!generic.active());

        controller.connectBus(); QTRY_VERIFY(controller.canStart());
        QVERIFY(!controller.setSlaveType("mit"));
        QCOMPARE(controller.slaveType(), QString("ecat2canfd"));
        generic.connectBus(1); QVERIFY(!generic.active());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
        QCOMPARE(owner.mode(), BusSessionCoordinator::Mode::Idle);
    }

    void gatewayDiagnosticsClearAfterRecoveryWithoutHidingLocalInputErrors()
    {
        BusSessionCoordinator owner;
        DamiaoMitController controller(owner, std::make_unique<damiao_test::Session>());
        controller.setSlaveType("ecat2canfd"); controller.setNicName("fake"); controller.connectBus();
        QTRY_VERIFY(controller.canStart());
        QSignalSpy logs(&controller, &DamiaoMitController::logAppended);
        controller.canfd()->applyConfiguration(1, {});
        QVERIFY(!controller.errorText().isEmpty());
        const auto gatewayError = controller.errorText();
        int matchingLines = 0;
        for (const auto& arguments : logs)
            if (arguments.front().toString() == gatewayError) ++matchingLines;
        QCOMPARE(matchingLines, 1);

        controller.canfd()->readConfiguration(1);
        QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.errorText().isEmpty());

        QVERIFY(!controller.sendControl(0, "800", controlValues()));
        const auto inputError = controller.errorText();
        QVERIFY(!inputError.isEmpty());
        QTest::qWait(50); // A 33 ms diagnostic refresh must preserve facade validation errors.
        QCOMPARE(controller.errorText(), inputError);
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
    }

    void legacyRetainsDecimalSlotAndGlobalControlContract()
    {
        BusSessionCoordinator owner;
        QString ownershipError;
        QVERIFY(owner.tryAcquire(BusSessionCoordinator::Mode::MitDebug, ownershipError));
        DamiaoMitController controller(owner, std::make_unique<damiao_test::Session>(false));
        controller.setLegacyConnected(true);
        QSignalSpy sent(&controller, &DamiaoMitController::legacySendRequested);
        QSignalSpy cleared(&controller, &DamiaoMitController::legacyClearRequested);
        QSignalSpy enabled(&controller, &DamiaoMitController::legacyEnableRequested);
        QSignalSpy disabled(&controller, &DamiaoMitController::legacyDisableRequested);
        QVERIFY(controller.sendControl(0, "1", controlValues()));
        QVERIFY(controller.sendControl(1, "9", controlValues()));
        QCOMPARE(sent.count(), 2);
        QCOMPARE(sent.at(1).at(0).toInt(), 1);
        QCOMPARE(sent.at(1).at(1).toInt(), 9);
        for (const auto& id : {QString("0"), QString("10"), QString("0x9"), QString("A")})
            QVERIFY(!controller.sendControl(0, id, controlValues()));
        QVERIFY(!controller.sendControl(2, "1", controlValues()));
        QCOMPARE(sent.count(), 2);
        QVERIFY(controller.enableMotor(0, "1"));
        QVERIFY(controller.disableMotor(1, "9"));
        QCOMPARE(enabled.count(), 1); QCOMPARE(disabled.count(), 1);
        // Legacy global actions do not address a slot, including while its
        // editor is empty or invalid. Targeted control/reset still validate it.
        QVERIFY(controller.enableMotor(-1, ""));
        QVERIFY(controller.enableMotor(2, "invalid"));
        QVERIFY(controller.disableMotor(-1, ""));
        QVERIFY(controller.disableMotor(2, "invalid"));
        QCOMPARE(enabled.count(), 3); QCOMPARE(disabled.count(), 3);
        QVERIFY(!controller.sendControl(-1, "", controlValues()));
        QVERIFY(!controller.stopSending(2, "invalid"));
        QCOMPARE(cleared.count(), 0);
        QVERIFY(controller.stopSending(1, "9")); QCOMPARE(cleared.count(), 1);
        QCOMPARE(cleared.at(0).at(0).toInt(), 1); QCOMPARE(cleared.at(0).at(1).toInt(), 9);
        controller.setLegacyConnected(false);
        QVERIFY(owner.release(BusSessionCoordinator::Mode::MitDebug));
        QVERIFY(!controller.sendControl(0, "1", controlValues()));
        QCOMPARE(sent.count(), 2);
    }

    void gatewayUsesHexIdsAndConsumesSingleControlOnce()
    {
        BusSessionCoordinator owner;
        auto fake = std::make_unique<damiao_test::Session>(); auto* session = fake.get();
        DamiaoMitController controller(owner, std::move(fake));
        QVERIFY(controller.setSlaveType("ecat2canfd"));
        controller.setNicName("fake"); controller.connectBus(); QTRY_VERIFY(controller.canStart());
        QVariantList configs;
        for (int channel = 0; channel < 2; ++channel)
            configs.push_back(QVariantMap{{"channel", channel}, {"enabled", true},
                {"format", channel == 0 ? 2 : 1}, {"nominalBitrate", 1000000},
                {"dataBitrate", 5000000}, {"options", 5}});
        controller.canfd()->applyConfiguration(1, configs);
        QTRY_VERIFY(!controller.busy());
        controller.start(2000, false, 0); QTRY_VERIFY(controller.running());
        QVERIFY(!controller.sendControl(0, "800", controlValues()));
        QVERIFY(!controller.sendControl(0, "-1", controlValues()));
        QVERIFY(!controller.sendControl(2, "1", controlValues()));
        auto invalid = controlValues(); invalid["kp"] = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!controller.sendControl(0, "1", invalid));
        QVERIFY(!controller.sendRaw(0, "1", "00 11 22 33 44 55 66"));
        QVERIFY(!controller.sendRaw(0, "1", "00 11 22 33 44 55 66 77 88"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        QVERIFY(controller.sendControl(0, "0", controlValues()));
        auto frames = advanceSingleSubmission(*session);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].toMap().value("idHex").toString(), QString("000"));
        QCOMPARE(frames[0].toMap().value("dataHex").toString(), QString("7F FF 7F F0 00 00 07 FF"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        QTRY_VERIFY(!controller.targetLocked());
        QVERIFY(controller.sendControl(1, "0x7FF", controlValues()));
        frames = advanceSingleSubmission(*session);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].toMap().value("channel").toInt(), 1);
        QCOMPARE(frames[0].toMap().value("idHex").toString(), QString("7FF"));
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
    }

    void disableReplacesQueuedAndPeriodicControlAfterEmptyCycle()
    {
        BusSessionCoordinator owner;
        auto fake = std::make_unique<damiao_test::Session>(); auto* session = fake.get();
        DamiaoMitController controller(owner, std::move(fake));
        controller.setSlaveType("ecat2canfd"); controller.setNicName("fake"); controller.connectBus();
        QTRY_VERIFY(controller.canStart()); controller.start(2000, true, 0); QTRY_VERIFY(controller.running());
        QVERIFY(controller.enableMotor(0, "123"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        auto frames = outgoingFrames(session->cycle());
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].toMap().value("dataHex").toString(), QString("FF FF FF FF FF FF FF FC"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty()); QTRY_VERIFY(!controller.targetLocked());

        QVERIFY(controller.sendControl(0, "123", controlValues(), true, 3001));
        QVERIFY(controller.periodicSending()); QCOMPARE(controller.periodicIntervalUs(), 4000);
        QVERIFY(controller.targetLocked());
        QVERIFY(!controller.sendControl(1, "456", controlValues()));
        QVERIFY(controller.disableMotor(0, "123"));
        QVERIFY(!controller.periodicSending());
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        frames = outgoingFrames(session->cycle());
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].toMap().value("idHex").toString(), QString("123"));
        QCOMPARE(frames[0].toMap().value("dataHex").toString(), QString("FF FF FF FF FF FF FF FD"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        QTRY_VERIFY(!controller.targetLocked());

        QVERIFY(controller.sendControl(0, "123", controlValues()));
        QVERIFY(controller.disableMotor(0, "123"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        frames = outgoingFrames(session->cycle());
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].toMap().value("dataHex").toString(), QString("FF FF FF FF FF FF FF FD"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
    }

    void stopSendingAndBadWkcNeverResumeOldCommands()
    {
        BusSessionCoordinator owner;
        auto fake = std::make_unique<damiao_test::Session>(); auto* session = fake.get();
        DamiaoMitController controller(owner, std::move(fake));
        controller.setSlaveType("ecat2canfd"); controller.setNicName("fake"); controller.connectBus();
        QTRY_VERIFY(controller.canStart()); controller.start(); QTRY_VERIFY(controller.running());
        QVERIFY(controller.sendControl(0, "123", controlValues(), true, 2000));
        QVERIFY(controller.stopSending(0, "123"));
        QVERIFY(!controller.periodicSending());
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        QVERIFY(controller.sendControl(0, "123", controlValues()));
        QVERIFY(controller.stopSending(0, "123"));
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        QVERIFY(controller.sendControl(0, "123", controlValues(), true, 2000));
        session->cycle(0); QTRY_VERIFY(!controller.periodicSending());
        QVERIFY(outgoingFrames(session->cycle(3)).isEmpty());
        QVERIFY(!controller.periodicSending());
        controller.stop(); QTRY_VERIFY(controller.canStart());
        controller.start(); QTRY_VERIFY(controller.running());
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
    }

    void configurationReadInvalidationStopsTransmission_data()
    {
        QTest::addColumn<int>("defect");
        QTest::addColumn<bool>("periodic");
        QTest::newRow("partial-read-failure-periodic") << 0 << true;
        QTest::newRow("partial-read-failure-queued") << 0 << false;
        QTest::newRow("channel-disabled") << 1 << true;
        QTest::newRow("channel-dirty") << 2 << true;
        QTest::newRow("channel-error") << 3 << true;
    }

    void configurationReadInvalidationStopsTransmission()
    {
        QFETCH(int, defect); QFETCH(bool, periodic);
        BusSessionCoordinator owner;
        auto fake = std::make_unique<damiao_test::Session>(); auto* session = fake.get();
        DamiaoMitController controller(owner, std::move(fake));
        controller.setSlaveType("ecat2canfd"); controller.setNicName("fake"); controller.connectBus();
        QTRY_VERIFY(controller.canStart()); controller.start(); QTRY_VERIFY(controller.running());
        QVERIFY(controller.sendControl(0, "123", controlValues(), periodic, 2000));
        QVERIFY(controller.targetLocked());
        // Failing CAN1 after CAN0 has already been read also invalidates the
        // request's partial CAN0 result and must stop that channel's output.
        if (defect == 0) session->failReadChannel.store(1);
        if (defect == 1) session->setConfigurationField(0, 1, 0);
        if (defect == 2) session->setConfigurationField(0, 7, 1);
        if (defect == 3) session->setConfigurationField(0, 8, 3);
        controller.canfd()->readConfiguration(1);
        QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.running());
        QVERIFY(!controller.periodicSending());
        QVERIFY(!controller.targetLocked());
        if (defect == 0)
            QVERIFY(!controller.canfd()->channelConfigs()[0].toMap().value("verified").toBool());
        QVERIFY(outgoingFrames(session->cycle()).isEmpty());

        session->failReadChannel.store(-1);
        session->setConfigurationField(0, 1, 1);
        session->setConfigurationField(0, 7, 0);
        session->setConfigurationField(0, 8, 0);
        controller.canfd()->readConfiguration(1);
        QTRY_VERIFY(!controller.busy());
        QVERIFY(controller.canfd()->channelConfigs()[0].toMap().value("verified").toBool());
        QVERIFY(!controller.periodicSending());
        QVERIFY(!controller.targetLocked());
        QVERIFY(advanceSingleSubmission(*session).isEmpty());
        controller.disconnectBus(); QTRY_VERIFY(!controller.active());
    }
};

QTEST_GUILESS_MAIN(DamiaoMitTests)
#include "DamiaoMitTests.moc"

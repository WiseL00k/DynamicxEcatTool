#include "Backend/Explorer/EsiParser.h"
#include "Backend/Ethercat/BusSessionCoordinator.h"
#include "Backend/Explorer/EsiRepository.h"
#include "Backend/Explorer/PdoValueCodec.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtTest>
#include <QTemporaryDir>

#include <algorithm>

using namespace explorer;

class ExplorerTests : public QObject
{
    Q_OBJECT

private slots:
    void rmFixtureParsesExpectedInventory();
    void parserSupportsNamespacesMultipleDevicesAndNumericFormats();
    void damagedXmlIsRejected();
    void repositoryEnforcesIdentityNameAndMappingRules();
    void codecHandlesTypedAndUnalignedFields();
    void codecRejectsInvalidInputWithoutMutation();
    void repositoryRejectsPartialDevicesFromDamagedXml();
    void codecSupportsOnlineCoeAliases();
    void stablePdoIdsIncludeProcessOffsets();
    void sessionCoordinatorEnforcesExclusiveOwnership();
};

void ExplorerTests::rmFixtureParsesExpectedInventory()
{
    const QString fixture =
        QDir(QStringLiteral(DYNAMICX_TEST_ESI_DIR)).filePath(QStringLiteral("RM.xml"));
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    const EsiParseResult parsed = EsiParser().parseFile(fixture);
    QVERIFY2(parsed.ok(), parsed.diagnostics.isEmpty()
                              ? "RM.xml parsing failed without a diagnostic"
                              : qPrintable(parsed.diagnostics.first().message));
    QCOMPARE(parsed.devices.size(), 1);

    const EsiDevice& device = parsed.devices.first();
    QCOMPARE(device.name, QStringLiteral("RM"));
    QCOMPARE(device.vendorId, quint32(0));
    QCOMPARE(device.productCode, quint32(0xAB123));
    QCOMPARE(device.revisionNo, quint32(2));
    QCOMPARE(device.dataTypes.size(), 66);
    QCOMPARE(device.objects.size(), 52);

    int rxPdos = 0;
    int txPdos = 0;
    for (const PdoMapping& mapping : device.pdoMappings) {
        if (mapping.direction == PdoDirection::Rx) {
            ++rxPdos;
        } else {
            ++txPdos;
        }
    }
    QCOMPARE(rxPdos, 4);
    QCOMPARE(txPdos, 16);
    QCOMPARE(device.rxPdoBits, qsizetype(37 * 8));
    QCOMPARE(device.txPdoBits, qsizetype(173 * 8));

    int readOnlyLeaves = 0;
    int writableLeaves = 0;
    for (const ObjectDictionaryEntry& object : device.objects) {
        if (object.subItems.isEmpty()) {
            readOnlyLeaves += object.access == AccessMode::ReadOnly ? 1 : 0;
            writableLeaves += object.access == AccessMode::WriteOnly
                    || object.access == AccessMode::ReadWrite ? 1 : 0;
            continue;
        }
        for (const OdSubItem& subItem : object.subItems) {
            readOnlyLeaves += subItem.access == AccessMode::ReadOnly ? 1 : 0;
            writableLeaves += subItem.access == AccessMode::WriteOnly
                    || subItem.access == AccessMode::ReadWrite ? 1 : 0;
        }
    }
    QVERIFY(readOnlyLeaves > 0);
    QCOMPARE(writableLeaves, 0);
}

void ExplorerTests::parserSupportsNamespacesMultipleDevicesAndNumericFormats()
{
    const QByteArray xml = R"xml(
        <EtherCATInfo xmlns="urn:ethercat:test">
          <Vendor>
            <Id>#x2A</Id>
            <Name LcId="1031">Hersteller</Name>
            <Name LcId="1033">Vendor</Name>
          </Vendor>
          <Descriptions><Devices>
            <Device>
              <Type ProductCode="0x10" RevisionNo="2">DriveType</Type>
              <Name LcId="1033">Alpha Drive</Name>
              <Mailbox><CoE SdoInfo="true" PdoUpload="1"/></Mailbox>
              <RxPdo Sm="2" Fixed="true">
                <Index>#x1600</Index><Name>Command</Name>
                <Entry><Index>0x7000</Index><SubIndex>1</SubIndex>
                  <BitLen>12</BitLen><Name>Target</Name><DataType>INT</DataType>
                </Entry>
              </RxPdo>
            </Device>
            <Device>
              <Type ProductCode="17" RevisionNo="#x3">IoType</Type>
              <Name>Beta IO</Name>
              <TxPdo Sm="3"><Index>0x1A00</Index>
                <Entry><Index>#x6000</Index><SubIndex>0</SubIndex>
                  <BitLen>1</BitLen><DataType>BOOL</DataType>
                </Entry>
              </TxPdo>
            </Device>
          </Devices></Descriptions>
        </EtherCATInfo>)xml";

    QBuffer buffer;
    buffer.setData(xml);
    const EsiParseResult parsed = EsiParser().parse(&buffer, QStringLiteral("inline.xml"));
    QVERIFY2(parsed.ok(), parsed.diagnostics.isEmpty()
                              ? "inline ESI parsing failed"
                              : qPrintable(parsed.diagnostics.first().message));
    QCOMPARE(parsed.devices.size(), 2);
    QCOMPARE(parsed.devices.at(0).vendorId, quint32(42));
    QCOMPARE(parsed.devices.at(0).vendorName, QStringLiteral("Vendor"));
    QCOMPARE(parsed.devices.at(0).productCode, quint32(16));
    QCOMPARE(parsed.devices.at(0).revisionNo, quint32(2));
    QVERIFY(parsed.devices.at(0).coeSupported);
    QVERIFY(parsed.devices.at(0).sdoInfoSupported);
    QVERIFY(parsed.devices.at(0).pdoUploadSupported);
    QCOMPARE(parsed.devices.at(0).rxPdoBits, qsizetype(12));
    QCOMPARE(parsed.devices.at(1).productCode, quint32(17));
    QCOMPARE(parsed.devices.at(1).revisionNo, quint32(3));
    QCOMPARE(parsed.devices.at(1).txPdoBits, qsizetype(1));

    quint64 value = 0;
    QVERIFY(EsiParser::parseUnsigned(QStringLiteral("#xAB123"), &value));
    QCOMPARE(value, quint64(0xAB123));
    QVERIFY(EsiParser::parseUnsigned(QStringLiteral("0x20"), &value));
    QCOMPARE(value, quint64(32));
    QVERIFY(EsiParser::parseUnsigned(QStringLiteral("33"), &value));
    QCOMPARE(value, quint64(33));
    QVERIFY(!EsiParser::parseUnsigned(QStringLiteral("0xbroken"), &value));
}

void ExplorerTests::damagedXmlIsRejected()
{
    QBuffer buffer;
    buffer.setData("<EtherCATInfo><Vendor><Id>1</Vendor>");
    const EsiParseResult parsed = EsiParser().parse(&buffer, QStringLiteral("broken.xml"));
    QVERIFY(!parsed.ok());
    QVERIFY(!parsed.diagnostics.isEmpty());
    QCOMPARE(parsed.diagnostics.last().severity, ParseDiagnostic::Severity::Error);
    QVERIFY(parsed.diagnostics.last().line > 0);
}

void ExplorerTests::repositoryEnforcesIdentityNameAndMappingRules()
{
    EsiDevice device;
    device.name = QStringLiteral("RM Drive");
    device.typeName = QStringLiteral("RM-Type");
    device.vendorId = 7;
    device.productCode = 8;
    device.revisionNo = 9;
    PdoMapping pdo;
    pdo.direction = PdoDirection::Rx;
    pdo.index = 0x1600;
    pdo.entries.push_back({0x7000, 1, QStringLiteral("Target"),
                           QStringLiteral("INT"), 12, 0, 0});
    pdo.bitLength = 12;
    device.pdoMappings.push_back(pdo);

    EsiRepository repository;
    repository.replaceDevices({device});
    OnlineSlaveIdentity online;
    online.name = QStringLiteral("unrelated");
    online.vendorId = 7;
    online.productCode = 8;
    online.revisionNo = 9;
    online.activeMappingKnown = true;
    online.activePdoEntries = flattenedPdoSignature(device);

    EsiMatchResult match = repository.match(online);
    QVERIFY(match.matched);
    QVERIFY(match.trusted);
    QCOMPARE(match.kind, EsiMatchKind::ExactIdentity);

    online.vendorId = 99;
    online.name = QStringLiteral("  rm   DRIVE ");
    match = repository.match(online);
    QVERIFY(match.matched);
    QVERIFY(match.trusted);
    QCOMPARE(match.kind, EsiMatchKind::UniqueNormalizedName);

    online.activePdoEntries[0].bitLength = 16;
    match = repository.match(online);
    QVERIFY(match.matched);
    QVERIFY(!match.trusted);
    QVERIFY(!match.mappingCompatible);

    repository.replaceDevices({device, device});
    online.vendorId = 7;
    online.name = device.name;
    online.activePdoEntries = flattenedPdoSignature(device);
    match = repository.match(online);
    QVERIFY(!match.matched);
    QVERIFY(match.reason.contains(QStringLiteral("Multiple")));
}

void ExplorerTests::codecHandlesTypedAndUnalignedFields()
{
    QByteArray image(16, '\0');

    QVERIFY(PdoValueCodec::encode(true, QStringLiteral("BOOL"), 1, &image, 1).ok);
    PdoDecodeResult decoded =
        PdoValueCodec::decode(image, 1, 1, QStringLiteral("BOOL"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toBool(), true);

    QVERIFY(PdoValueCodec::encode(-17, QStringLiteral("INT"), 12, &image, 3).ok);
    decoded = PdoValueCodec::decode(image, 3, 12, QStringLiteral("INT"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toLongLong(), qint64(-17));

    QVERIFY(PdoValueCodec::encode(QStringLiteral("0x1234"),
                                  QStringLiteral("UINT"), 13, &image, 19).ok);
    decoded = PdoValueCodec::decode(image, 19, 13, QStringLiteral("UINT"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toULongLong(), quint64(0x1234));

    QByteArray realImage(4, '\0');
    QVERIFY(PdoValueCodec::encode(QStringLiteral("1.25"),
                                  QStringLiteral("REAL"), 32, &realImage, 0).ok);
    decoded = PdoValueCodec::decode(realImage, 0, 32, QStringLiteral("REAL"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toFloat(), 1.25F);

    QByteArray textImage(8, '\0');
    QVERIFY(PdoValueCodec::encode(QStringLiteral("RM"),
                                  QStringLiteral("VISIBLE_STRING"), 64, &textImage, 0).ok);
    decoded = PdoValueCodec::decode(textImage, 0, 64, QStringLiteral("VISIBLE_STRING"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toString(), QStringLiteral("RM"));

    QByteArray rawImage(3, '\0');
    QVERIFY(PdoValueCodec::encode(QStringLiteral("AB01"),
                                  QStringLiteral("OCTET_STRING"), 9, &rawImage, 3).ok);
    decoded = PdoValueCodec::decode(rawImage, 3, 9, QStringLiteral("OCTET_STRING"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toByteArray(), QByteArray::fromHex("ab01"));
}

void ExplorerTests::codecRejectsInvalidInputWithoutMutation()
{
    QByteArray image = QByteArray::fromHex("a55a");
    const QByteArray original = image;
    QVERIFY(!PdoValueCodec::encode(256, QStringLiteral("USINT"), 8, &image, 0).ok);
    QCOMPARE(image, original);
    QVERIFY(!PdoValueCodec::encode(-1, QStringLiteral("UINT"), 8, &image, 0).ok);
    QCOMPARE(image, original);
    QVERIFY(!PdoValueCodec::encode(QStringLiteral("too long"),
                                   QStringLiteral("VISIBLE_STRING"), 16, &image, 0).ok);
    QCOMPARE(image, original);
    QVERIFY(!PdoValueCodec::decode(image, 15, 8, QStringLiteral("UINT")).ok);
}

void ExplorerTests::repositoryRejectsPartialDevicesFromDamagedXml()
{
    const QString invalidDirectory = QDir(
        QStringLiteral(DYNAMICX_TEST_ESI_DIR)).absoluteFilePath(
            QStringLiteral("../invalid"));
    QVERIFY(QFileInfo::exists(
        QDir(invalidDirectory).filePath(QStringLiteral("partial.xml"))));

    EsiRepository repository;
    const EsiRepositoryIndexResult indexed =
        repository.indexDirectory(invalidDirectory);
    QVERIFY(!indexed.ok());
    QCOMPARE(indexed.filesScanned, 1);
    QCOMPARE(indexed.devicesIndexed, 0);
    QVERIFY(repository.devices().isEmpty());
}

void ExplorerTests::codecSupportsOnlineCoeAliases()
{
    QByteArray integerImage(4, '\0');
    QVERIFY(PdoValueCodec::encode(
        -2, QStringLiteral("INTEGER24"), 24, &integerImage, 3).ok);
    PdoDecodeResult decoded = PdoValueCodec::decode(
        integerImage, 3, 24, QStringLiteral("INTEGER24"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toLongLong(), qint64(-2));

    QByteArray unsignedImage(3, '\0');
    QVERIFY(PdoValueCodec::encode(
        QStringLiteral("0xABCDE"), QStringLiteral("UNSIGNED24"),
        24, &unsignedImage, 0).ok);
    decoded = PdoValueCodec::decode(
        unsignedImage, 0, 24, QStringLiteral("UNSIGNED24"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toULongLong(), quint64(0xABCDE));

    QByteArray realImage(4, '\0');
    QVERIFY(PdoValueCodec::encode(
        QStringLiteral("-2.5"), QStringLiteral("REAL32"),
        32, &realImage, 0).ok);
    decoded = PdoValueCodec::decode(
        realImage, 0, 32, QStringLiteral("REAL32"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toFloat(), -2.5F);

    QByteArray stringImage(8, '\0');
    QVERIFY(PdoValueCodec::encode(
        QStringLiteral("RM"), QStringLiteral("STRING(8)"),
        64, &stringImage, 0).ok);
    decoded = PdoValueCodec::decode(
        stringImage, 0, 64, QStringLiteral("STRING(8)"));
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.toString(), QStringLiteral("RM"));
}

void ExplorerTests::stablePdoIdsIncludeProcessOffsets()
{
    const QString first = makePdoStableId(
        1, PdoDirection::Rx, 0x1600, 0x7000, 1, 0);
    const QString repeated = makePdoStableId(
        1, PdoDirection::Rx, 0x1600, 0x7000, 1, 16);
    QVERIFY(first != repeated);
}

void ExplorerTests::sessionCoordinatorEnforcesExclusiveOwnership()
{
    Backend::BusSessionCoordinator coordinator;
    QString error;
    QVERIFY(coordinator.tryAcquire(
        Backend::BusSessionCoordinator::Mode::Test, error));
    QVERIFY(coordinator.active());
    QVERIFY(!coordinator.tryAcquire(
        Backend::BusSessionCoordinator::Mode::Explorer, error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!coordinator.release(
        Backend::BusSessionCoordinator::Mode::Communication));
    QVERIFY(coordinator.active());
    QVERIFY(coordinator.release(
        Backend::BusSessionCoordinator::Mode::Test));
    QVERIFY(!coordinator.active());
    coordinator.forceIdle();
    QVERIFY(!coordinator.active());
}

QTEST_APPLESS_MAIN(ExplorerTests)

#include "ExplorerTests.moc"

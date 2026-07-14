#include "Backend/Explorer/EsiParser.h"
#include "Backend/Ethercat/BusSessionCoordinator.h"
#include "Backend/Explorer/EsiRepository.h"
#include "Backend/Explorer/ExplorerModels.h"
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
    void parserAnnotatesOnlyDeclaredArrayRange();
    void damagedXmlIsRejected();
    void repositoryEnforcesIdentityNameAndMappingRules();
    void codecHandlesTypedAndUnalignedFields();
    void codecRejectsInvalidInputWithoutMutation();
    void repositoryRejectsPartialDevicesFromDamagedXml();
    void codecSupportsOnlineCoeAliases();
    void stablePdoIdsIncludeProcessOffsets();
    void pdoVariableGroupsAggregateTrustedArrays();
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

    const auto arrayTypePosition = device.dataTypes.constFind(QStringLiteral("DT7000ARR"));
    QVERIFY(arrayTypePosition != device.dataTypes.constEnd());
    const DataTypeDefinition& arrayType = arrayTypePosition.value();
    QVERIFY(arrayType.isArray());
    QCOMPARE(arrayType.baseType, QStringLiteral("INT"));
    QCOMPARE(arrayType.arrayLowerBound, 1);
    QCOMPARE(arrayType.arrayElements, 8);
    QCOMPARE(arrayType.bitSize, 128);
    QCOMPARE(arrayType.arrayElementBitSize, 16);

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

    const PdoMapping* commandArray = nullptr;
    for (const PdoMapping& mapping : device.pdoMappings) {
        if (mapping.index == 0x1601) {
            commandArray = &mapping;
            break;
        }
    }
    QVERIFY(commandArray != nullptr);
    QCOMPARE(commandArray->entries.size(), 8);
    for (qsizetype i = 0; i < commandArray->entries.size(); ++i) {
        const PdoEntry& entry = commandArray->entries.at(i);
        QCOMPARE(entry.index, quint16(0x7000));
        QCOMPARE(entry.subIndex, quint8(i + 1));
        QCOMPARE(entry.arrayName, QStringLiteral("can0_motor_commnads"));
        QCOMPARE(entry.arrayLowerBound, 1);
        QCOMPARE(entry.arrayElements, 8);
        QCOMPARE(entry.arrayElementIndex, int(i));
    }

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

void ExplorerTests::parserAnnotatesOnlyDeclaredArrayRange()
{
    const QByteArray xml = R"xml(
        <EtherCATInfo>
          <Vendor><Id>1</Id></Vendor>
          <Descriptions><Devices><Device>
            <Type ProductCode="1" RevisionNo="1">ArrayDevice</Type>
            <Profile><Dictionary>
              <DataTypes>
                <DataType>
                  <Name>Array3</Name><BaseType>USINT</BaseType><BitSize>24</BitSize>
                  <ArrayInfo><LBound>2</LBound><Elements>3</Elements></ArrayInfo>
                </DataType>
                <DataType>
                  <Name>Parent</Name><BitSize>24</BitSize>
                  <SubItem><Name>Elements</Name><Type>Array3</Type><BitSize>24</BitSize></SubItem>
                </DataType>
                <DataType>
                  <Name>Plain</Name><BaseType>USINT</BaseType><BitSize>8</BitSize>
                </DataType>
                <DataType>
                  <Name>ParentPlain</Name><BitSize>8</BitSize>
                  <SubItem><Name>Value</Name><Type>Plain</Type><BitSize>8</BitSize></SubItem>
                </DataType>
              </DataTypes>
              <Objects>
                <Object><Index>#x7000</Index><Name>Array object</Name>
                  <Type>Parent</Type><BitSize>24</BitSize></Object>
                <Object><Index>#x7001</Index><Name>Plain object</Name>
                  <Type>ParentPlain</Type><BitSize>8</BitSize></Object>
              </Objects>
            </Dictionary></Profile>
            <RxPdo Sm="2"><Index>#x1600</Index>
              <Entry><Index>#x7000</Index><SubIndex>1</SubIndex><BitLen>8</BitLen></Entry>
              <Entry><Index>#x7000</Index><SubIndex>2</SubIndex><BitLen>8</BitLen></Entry>
              <Entry><Index>#x7000</Index><SubIndex>4</SubIndex><BitLen>8</BitLen></Entry>
              <Entry><Index>#x7000</Index><SubIndex>5</SubIndex><BitLen>8</BitLen></Entry>
              <Entry><Index>#x7001</Index><SubIndex>1</SubIndex><BitLen>8</BitLen></Entry>
            </RxPdo>
          </Device></Devices></Descriptions>
        </EtherCATInfo>)xml";

    QBuffer buffer;
    buffer.setData(xml);
    const EsiParseResult parsed =
        EsiParser().parse(&buffer, QStringLiteral("array-range.xml"));
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.devices.size(), 1);

    const EsiDevice& device = parsed.devices.first();
    const DataTypeDefinition arrayType =
        device.dataTypes.value(QStringLiteral("Array3"));
    QVERIFY(arrayType.isArray());
    QCOMPARE(arrayType.arrayElementBitSize, 8);
    const DataTypeDefinition plainType =
        device.dataTypes.value(QStringLiteral("Plain"));
    QVERIFY(!plainType.isArray());
    QCOMPARE(plainType.arrayElementBitSize, 0);

    const QVector<PdoEntry>& entries = device.pdoMappings.first().entries;
    const auto findEntry = [&entries](quint16 index, quint8 subIndex) {
        return std::find_if(entries.cbegin(), entries.cend(),
                            [index, subIndex](const PdoEntry& entry) {
            return entry.index == index && entry.subIndex == subIndex;
        });
    };

    QCOMPARE(findEntry(0x7000, 1)->arrayElementIndex, -1);
    QCOMPARE(findEntry(0x7000, 2)->arrayElementIndex, 0);
    QCOMPARE(findEntry(0x7000, 2)->arrayName, QStringLiteral("Array object"));
    QCOMPARE(findEntry(0x7000, 4)->arrayElementIndex, 2);
    QCOMPARE(findEntry(0x7000, 5)->arrayElementIndex, -1);
    QCOMPARE(findEntry(0x7001, 1)->arrayElementIndex, -1);
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

void ExplorerTests::pdoVariableGroupsAggregateTrustedArrays()
{
    auto arrayElement = [](int elementIndex,
                           PdoDirection direction = PdoDirection::Rx) {
        PdoVariable variable;
        variable.slaveAddress = 1;
        variable.direction = direction;
        variable.pdoIndex = direction == PdoDirection::Rx ? 0x1600 : 0x1a00;
        variable.pdoName = QStringLiteral("Commands");
        variable.index = direction == PdoDirection::Rx ? 0x7000 : 0x6000;
        variable.subIndex = static_cast<quint8>(elementIndex + 1);
        variable.name = elementIndex == 0
            ? QStringLiteral("New array subitem")
            : QStringLiteral("Motor 2");
        variable.dataType = QStringLiteral("INT");
        variable.bitLength = 16;
        variable.bitOffset = elementIndex * 16;
        variable.arrayName = QStringLiteral("can0_motor_commnads");
        variable.arrayLowerBound = 1;
        variable.arrayElements = 2;
        variable.arrayElementIndex = elementIndex;
        variable.displayValue = QString::number(elementIndex + 10);
        variable.writable = direction == PdoDirection::Rx;
        variable.stableId = makePdoStableId(
            variable.slaveAddress, direction, variable.pdoIndex,
            variable.index, variable.subIndex, variable.bitOffset);
        return variable;
    };

    ExplorerPdoVariableGroupModel model;
    const QVector<PdoVariable> rxVariables = {arrayElement(0), arrayElement(1)};
    model.setVariables(rxVariables, true);
    QCOMPARE(model.rowCount(), 1);
    const QModelIndex rxGroup = model.index(0);
    QCOMPARE(model.data(rxGroup, ExplorerPdoVariableGroupModel::NameRole).toString(),
             QStringLiteral("can0_motor_commnads"));
    QVERIFY(model.data(rxGroup, ExplorerPdoVariableGroupModel::IsArrayRole).toBool());
    QCOMPARE(model.data(rxGroup, ExplorerPdoVariableGroupModel::ElementCountRole).toInt(), 2);
    const QStringList labels = model.data(
        rxGroup, ExplorerPdoVariableGroupModel::ElementLabelsRole).toStringList();
    QCOMPARE(labels,
             QStringList({QStringLiteral("元素 1 · 0x01"),
                          QStringLiteral("Motor 2")}));
    QVERIFY(model.data(
        rxGroup, ExplorerPdoVariableGroupModel::SelectedWritableRole).toBool());

    const QString groupId = model.data(
        rxGroup, ExplorerPdoVariableGroupModel::GroupIdRole).toString();
    QVERIFY(model.selectElement(groupId, 1));
    QVERIFY(!model.selectElement(groupId, 2));
    QCOMPARE(model.data(
        rxGroup, ExplorerPdoVariableGroupModel::SelectedElementIndexRole).toInt(), 1);
    QCOMPARE(model.data(
        rxGroup, ExplorerPdoVariableGroupModel::SelectedStableIdRole).toString(),
        rxVariables.at(1).stableId);
    QVERIFY(model.updateValue(rxVariables.at(1).stableId, 42, QStringLiteral("42")));
    QCOMPARE(model.data(
        rxGroup, ExplorerPdoVariableGroupModel::SelectedDisplayValueRole).toString(),
        QStringLiteral("42"));
    QVERIFY(model.selectElement(groupId, 0));
    QCOMPARE(model.data(
        rxGroup, ExplorerPdoVariableGroupModel::SelectedDisplayValueRole).toString(),
        QStringLiteral("10"));
    QVERIFY(model.selectElement(groupId, 1));

    QVector<PdoVariable> refreshed = rxVariables;
    refreshed[1].displayValue = QStringLiteral("43");
    model.setVariables(refreshed, true);
    QCOMPARE(model.data(
        model.index(0),
        ExplorerPdoVariableGroupModel::SelectedElementIndexRole).toInt(), 1);
    QCOMPARE(model.data(
        model.index(0),
        ExplorerPdoVariableGroupModel::SelectedDisplayValueRole).toString(),
        QStringLiteral("43"));

    QVector<PdoVariable> txVariables = {
        arrayElement(0, PdoDirection::Tx),
        arrayElement(1, PdoDirection::Tx)};
    txVariables[0].writable = true;
    txVariables[1].writable = true;
    model.setVariables(txVariables, true);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.data(
        model.index(0),
        ExplorerPdoVariableGroupModel::SelectedWritableRole).toBool());

    model.setVariables(rxVariables, false);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(!model.data(
        model.index(0), ExplorerPdoVariableGroupModel::IsArrayRole).toBool());
    QVERIFY(!model.data(
        model.index(0),
        ExplorerPdoVariableGroupModel::SelectedWritableRole).toBool());

    QVector<PdoVariable> incomplete = {arrayElement(0)};
    model.setVariables(incomplete, true);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.data(
        model.index(0), ExplorerPdoVariableGroupModel::IsArrayRole).toBool());
    QVERIFY(!model.selectElement(QStringLiteral("missing"), 0));
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

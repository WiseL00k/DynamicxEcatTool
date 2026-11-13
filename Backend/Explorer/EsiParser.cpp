#include "Backend/Explorer/EsiParser.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace explorer {
namespace {

struct VendorInfo
{
    quint32 id{0};
    QString name;
    bool englishNameFound{false};
};

struct ObjectInfoSubItem
{
    int subIndex{-1};
    QString name;
    QString defaultValue;
};

QString fileErrorDiagnostic(const QFileDevice& file)
{
    QString reason;
    switch (file.error()) {
    case QFileDevice::PermissionsError: reason = QStringLiteral("访问权限不足"); break;
    case QFileDevice::OpenError: reason = QStringLiteral("文件无法打开，请检查路径、访问权限或文件占用情况"); break;
    case QFileDevice::ReadError: reason = QStringLiteral("文件读取失败"); break;
    case QFileDevice::ResourceError: reason = QStringLiteral("系统资源不足，无法访问文件"); break;
    case QFileDevice::TimeOutError: reason = QStringLiteral("文件访问超时"); break;
    case QFileDevice::AbortError: reason = QStringLiteral("文件访问已中止"); break;
    default: reason = QStringLiteral("文件访问失败"); break;
    }
    return reason + QStringLiteral("；原始诊断：") + file.errorString();
}

QString xmlErrorDiagnostic(const QXmlStreamReader& xml)
{
    QString reason;
    switch (xml.error()) {
    case QXmlStreamReader::NotWellFormedError:
        reason = QStringLiteral("XML 格式不正确，请检查标签、属性和字符编码"); break;
    case QXmlStreamReader::PrematureEndOfDocumentError:
        reason = QStringLiteral("XML 文档提前结束，请检查标签是否闭合及文件是否完整"); break;
    case QXmlStreamReader::UnexpectedElementError:
        reason = QStringLiteral("XML 包含当前位置不允许的元素"); break;
    default:
        reason = QStringLiteral("XML 解析失败"); break;
    }
    return QStringLiteral("%1（第 %2 行，第 %3 列）；原始诊断：%4")
        .arg(reason).arg(xml.lineNumber()).arg(xml.columnNumber()).arg(xml.errorString());
}

void addDiagnostic(QVector<ParseDiagnostic>& diagnostics,
                   ParseDiagnostic::Severity severity,
                   const QString& source,
                   const QXmlStreamReader& xml,
                   const QString& message)
{
    diagnostics.push_back({severity,
                           source,
                           static_cast<qint64>(xml.lineNumber()),
                           static_cast<qint64>(xml.columnNumber()),
                           message});
}

QString elementText(QXmlStreamReader& xml)
{
    return xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
}

bool booleanAttribute(const QXmlStreamAttributes& attributes, const QString& name)
{
    const QString value = attributes.value(name).toString().trimmed().toLower();
    return value == QStringLiteral("true") || value == QStringLiteral("1")
        || value == QStringLiteral("yes");
}

void assignLocalizedText(QXmlStreamReader& xml, QString& target, bool& englishFound)
{
    const QString locale = xml.attributes().value(QStringLiteral("LcId")).toString();
    const QString value = elementText(xml);
    const bool isEnglish = locale.isEmpty() || locale == QStringLiteral("1033");
    if ((target.isEmpty() && !value.isEmpty()) || (isEnglish && !englishFound)) {
        target = value;
    }
    if (isEnglish && !value.isEmpty()) {
        englishFound = true;
    }
}

bool readUnsignedElement(QXmlStreamReader& xml,
                         quint64& target,
                         QVector<ParseDiagnostic>& diagnostics,
                         const QString& source)
{
    const QString text = elementText(xml);
    if (EsiParser::parseUnsigned(text, &target)) {
        return true;
    }
    addDiagnostic(diagnostics,
                  ParseDiagnostic::Severity::Warning,
                  source,
                  xml,
                  QStringLiteral("无效的无符号整数“%1”").arg(text));
    return false;
}

void parseFlags(QXmlStreamReader& xml, AccessMode& access, QString& pdoMapping)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Access")) {
            access = accessModeFromText(elementText(xml));
        } else if (xml.name() == QStringLiteral("PdoMapping")) {
            pdoMapping = elementText(xml).toUpper();
        } else {
            xml.skipCurrentElement();
        }
    }
}

OdSubItem parseDataTypeSubItem(QXmlStreamReader& xml,
                               QVector<ParseDiagnostic>& diagnostics,
                               const QString& source)
{
    OdSubItem result;
    bool nameIsEnglish = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("SubIdx")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.subIndex = static_cast<quint8>(value);
            }
        } else if (xml.name() == QStringLiteral("Name")) {
            assignLocalizedText(xml, result.name, nameIsEnglish);
        } else if (xml.name() == QStringLiteral("Type")) {
            result.dataType = elementText(xml);
        } else if (xml.name() == QStringLiteral("BitSize")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.bitSize = static_cast<int>(value);
            }
        } else if (xml.name() == QStringLiteral("BitOffs")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.bitOffset = static_cast<int>(value);
            }
        } else if (xml.name() == QStringLiteral("Flags")) {
            parseFlags(xml, result.access, result.pdoMapping);
        } else {
            xml.skipCurrentElement();
        }
    }
    return result;
}

DataTypeDefinition parseDataType(QXmlStreamReader& xml,
                                 QVector<ParseDiagnostic>& diagnostics,
                                 const QString& source)
{
    DataTypeDefinition result;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Name")) {
            result.name = elementText(xml);
        } else if (xml.name() == QStringLiteral("BaseType")) {
            result.baseType = elementText(xml);
        } else if (xml.name() == QStringLiteral("BitSize")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.bitSize = static_cast<int>(value);
            }
        } else if (xml.name() == QStringLiteral("ArrayInfo")) {
            while (xml.readNextStartElement()) {
                quint64 value = 0;
                if (xml.name() == QStringLiteral("LBound")) {
                    if (readUnsignedElement(xml, value, diagnostics, source)) {
                        result.arrayLowerBound = static_cast<int>(value);
                    }
                } else if (xml.name() == QStringLiteral("Elements")) {
                    if (readUnsignedElement(xml, value, diagnostics, source)) {
                        result.arrayElements = static_cast<int>(value);
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QStringLiteral("SubItem")) {
            result.subItems.push_back(parseDataTypeSubItem(xml, diagnostics, source));
        } else {
            xml.skipCurrentElement();
        }
    }
    if (result.arrayElements > 0 && result.bitSize > 0
        && result.bitSize % result.arrayElements == 0) {
        result.arrayElementBitSize = result.bitSize / result.arrayElements;
    }
    return result;
}

void parseDataTypes(QXmlStreamReader& xml,
                    EsiDevice& device,
                    QVector<ParseDiagnostic>& diagnostics,
                    const QString& source)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("DataType")) {
            DataTypeDefinition dataType = parseDataType(xml, diagnostics, source);
            if (dataType.name.isEmpty()) {
                addDiagnostic(diagnostics,
                              ParseDiagnostic::Severity::Warning,
                              source,
                              xml,
                              QStringLiteral("对象字典中的 DataType 缺少 Name"));
            } else {
                device.dataTypes.insert(dataType.name, dataType);
            }
        } else {
            xml.skipCurrentElement();
        }
    }
}

void parseObjectInfoSubItem(QXmlStreamReader& xml,
                            ObjectInfoSubItem& result,
                            QVector<ParseDiagnostic>& diagnostics,
                            const QString& source)
{
    bool nameIsEnglish = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("SubIdx")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.subIndex = static_cast<int>(value);
            }
        } else if (xml.name() == QStringLiteral("Name")) {
            assignLocalizedText(xml, result.name, nameIsEnglish);
        } else if (xml.name() == QStringLiteral("Info")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QStringLiteral("DefaultValue")) {
                    result.defaultValue = elementText(xml);
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else if (xml.name() == QStringLiteral("DefaultValue")) {
            result.defaultValue = elementText(xml);
        } else {
            xml.skipCurrentElement();
        }
    }
}

void parseObjectInfo(QXmlStreamReader& xml,
                     QString& defaultValue,
                     QVector<ObjectInfoSubItem>& subItems,
                     QVector<ParseDiagnostic>& diagnostics,
                     const QString& source)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("DefaultValue")) {
            defaultValue = elementText(xml);
        } else if (xml.name() == QStringLiteral("SubItem")) {
            ObjectInfoSubItem item;
            parseObjectInfoSubItem(xml, item, diagnostics, source);
            subItems.push_back(item);
        } else {
            xml.skipCurrentElement();
        }
    }
}

ObjectDictionaryEntry parseObject(QXmlStreamReader& xml,
                                  QVector<ParseDiagnostic>& diagnostics,
                                  const QString& source,
                                  QVector<ObjectInfoSubItem>& infoSubItems)
{
    ObjectDictionaryEntry result;
    bool nameIsEnglish = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Index")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.index = static_cast<quint16>(value);
            }
        } else if (xml.name() == QStringLiteral("Name")) {
            assignLocalizedText(xml, result.name, nameIsEnglish);
        } else if (xml.name() == QStringLiteral("Type")) {
            result.dataType = elementText(xml);
        } else if (xml.name() == QStringLiteral("BitSize")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.bitSize = static_cast<int>(value);
            }
        } else if (xml.name() == QStringLiteral("Info")) {
            parseObjectInfo(xml, result.defaultValue, infoSubItems, diagnostics, source);
        } else if (xml.name() == QStringLiteral("Flags")) {
            parseFlags(xml, result.access, result.pdoMapping);
        } else {
            xml.skipCurrentElement();
        }
    }
    return result;
}

void resolveObjectSubItems(ObjectDictionaryEntry& object,
                           const QVector<ObjectInfoSubItem>& infoSubItems,
                           const QHash<QString, DataTypeDefinition>& dataTypes)
{
    const auto dataType = dataTypes.constFind(object.dataType);
    if (dataType != dataTypes.constEnd()) {
        for (const OdSubItem& subItem : dataType->subItems) {
            const auto referencedType = dataTypes.constFind(subItem.dataType);
            const bool expandableArray = referencedType != dataTypes.constEnd()
                && referencedType->isArray()
                && referencedType->arrayElementBitSize > 0
                && referencedType->arrayLowerBound >= 0
                && referencedType->arrayLowerBound <= 255
                && referencedType->arrayElements
                    <= 256 - referencedType->arrayLowerBound;
            if (!expandableArray) {
                object.subItems.push_back(subItem);
                continue;
            }

            for (int element = 0; element < referencedType->arrayElements; ++element) {
                OdSubItem expanded = subItem;
                expanded.subIndex = static_cast<quint8>(
                    referencedType->arrayLowerBound + element);
                if (!referencedType->baseType.isEmpty()) {
                    expanded.dataType = referencedType->baseType;
                }
                expanded.bitSize = referencedType->arrayElementBitSize;
                expanded.bitOffset = subItem.bitOffset
                    + element * referencedType->arrayElementBitSize;
                object.subItems.push_back(std::move(expanded));
            }
        }
    }

    for (qsizetype i = 0; i < infoSubItems.size(); ++i) {
        const ObjectInfoSubItem& info = infoSubItems.at(i);
        qsizetype targetIndex = -1;
        if (info.subIndex >= 0) {
            for (qsizetype j = 0; j < object.subItems.size(); ++j) {
                if (object.subItems.at(j).subIndex == info.subIndex) {
                    targetIndex = j;
                    break;
                }
            }
        } else if (i < object.subItems.size()) {
            targetIndex = i;
        }

        if (targetIndex < 0) {
            OdSubItem added;
            added.subIndex = static_cast<quint8>(info.subIndex >= 0 ? info.subIndex : i);
            object.subItems.push_back(added);
            targetIndex = object.subItems.size() - 1;
        }

        OdSubItem& target = object.subItems[targetIndex];
        if (!info.name.isEmpty()) {
            target.name = info.name;
        }
        if (!info.defaultValue.isEmpty()) {
            target.defaultValue = info.defaultValue;
        }
        if (target.access == AccessMode::None) {
            target.access = object.access;
        }
        if (target.pdoMapping.isEmpty()) {
            target.pdoMapping = object.pdoMapping;
        }
    }

    for (OdSubItem& subItem : object.subItems) {
        if (subItem.access == AccessMode::None) {
            subItem.access = object.access;
        }
        if (subItem.pdoMapping.isEmpty()) {
            subItem.pdoMapping = object.pdoMapping;
        }
    }
}

void parseObjects(QXmlStreamReader& xml,
                  EsiDevice& device,
                  QVector<ParseDiagnostic>& diagnostics,
                  const QString& source)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Object")) {
            QVector<ObjectInfoSubItem> infoSubItems;
            ObjectDictionaryEntry object = parseObject(xml, diagnostics, source, infoSubItems);
            resolveObjectSubItems(object, infoSubItems, device.dataTypes);
            device.objects.push_back(object);
        } else {
            xml.skipCurrentElement();
        }
    }
}

void parseDictionary(QXmlStreamReader& xml,
                     EsiDevice& device,
                     QVector<ParseDiagnostic>& diagnostics,
                     const QString& source)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("DataTypes")) {
            parseDataTypes(xml, device, diagnostics, source);
        } else if (xml.name() == QStringLiteral("Objects")) {
            parseObjects(xml, device, diagnostics, source);
        } else {
            xml.skipCurrentElement();
        }
    }
}

void parseProfile(QXmlStreamReader& xml,
                  EsiDevice& device,
                  QVector<ParseDiagnostic>& diagnostics,
                  const QString& source)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Dictionary")) {
            parseDictionary(xml, device, diagnostics, source);
        } else {
            xml.skipCurrentElement();
        }
    }
}

PdoEntry parsePdoEntry(QXmlStreamReader& xml,
                       QVector<ParseDiagnostic>& diagnostics,
                       const QString& source)
{
    PdoEntry result;
    bool nameIsEnglish = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Index")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.index = static_cast<quint16>(value);
            }
        } else if (xml.name() == QStringLiteral("SubIndex")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.subIndex = static_cast<quint8>(value);
            }
        } else if (xml.name() == QStringLiteral("BitLen")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.bitLength = static_cast<int>(value);
            }
        } else if (xml.name() == QStringLiteral("Name")) {
            assignLocalizedText(xml, result.name, nameIsEnglish);
        } else if (xml.name() == QStringLiteral("DataType")) {
            result.dataType = elementText(xml);
        } else {
            xml.skipCurrentElement();
        }
    }
    return result;
}

PdoMapping parsePdo(QXmlStreamReader& xml,
                    PdoDirection direction,
                    QVector<ParseDiagnostic>& diagnostics,
                    const QString& source)
{
    PdoMapping result;
    result.direction = direction;
    const QXmlStreamAttributes attributes = xml.attributes();
    result.fixed = booleanAttribute(attributes, QStringLiteral("Fixed"));
    result.mandatory = booleanAttribute(attributes, QStringLiteral("Mandatory"));
    bool smOk = false;
    result.syncManager = attributes.value(QStringLiteral("Sm")).toInt(&smOk);
    if (!smOk) {
        result.syncManager = -1;
    }

    bool nameIsEnglish = false;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Index")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                result.index = static_cast<quint16>(value);
            }
        } else if (xml.name() == QStringLiteral("Name")) {
            assignLocalizedText(xml, result.name, nameIsEnglish);
        } else if (xml.name() == QStringLiteral("Entry")) {
            PdoEntry entry = parsePdoEntry(xml, diagnostics, source);
            entry.pdoBitOffset = result.bitLength;
            result.bitLength += entry.bitLength;
            result.entries.push_back(entry);
        } else {
            xml.skipCurrentElement();
        }
    }
    return result;
}

void parseMailbox(QXmlStreamReader& xml, EsiDevice& device)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("CoE")) {
            device.coeSupported = true;
            device.sdoInfoSupported = booleanAttribute(xml.attributes(), QStringLiteral("SdoInfo"));
            device.pdoUploadSupported = booleanAttribute(xml.attributes(), QStringLiteral("PdoUpload"));
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

void enrichPdoEntries(EsiDevice& device)
{
    QHash<quint16, qsizetype> objectsByIndex;
    for (qsizetype i = 0; i < device.objects.size(); ++i) {
        objectsByIndex.insert(device.objects.at(i).index, i);
    }

    qsizetype rxOffset = 0;
    qsizetype txOffset = 0;
    for (PdoMapping& mapping : device.pdoMappings) {
        qsizetype& processOffset = mapping.direction == PdoDirection::Rx ? rxOffset : txOffset;
        for (PdoEntry& entry : mapping.entries) {
            entry.processBitOffset = processOffset + entry.pdoBitOffset;
            const auto objectPosition = objectsByIndex.constFind(entry.index);
            if (objectPosition == objectsByIndex.constEnd()) {
                continue;
            }
            const ObjectDictionaryEntry& object = device.objects.at(*objectPosition);
            const DataTypeDefinition* arrayType = nullptr;
            const auto parentType = device.dataTypes.constFind(object.dataType);
            if (parentType != device.dataTypes.constEnd()) {
                for (const OdSubItem& subItem : parentType->subItems) {
                    const auto referencedType = device.dataTypes.constFind(subItem.dataType);
                    if (referencedType == device.dataTypes.constEnd()
                        || !referencedType->isArray()) {
                        continue;
                    }
                    const qint64 first = referencedType->arrayLowerBound;
                    const qint64 end = first + referencedType->arrayElements;
                    if (entry.subIndex >= first && entry.subIndex < end) {
                        arrayType = &referencedType.value();
                        break;
                    }
                }
            }

            if (arrayType) {
                entry.arrayName = object.name;
                entry.arrayLowerBound = arrayType->arrayLowerBound;
                entry.arrayElements = arrayType->arrayElements;
                entry.arrayElementIndex = static_cast<int>(entry.subIndex)
                    - arrayType->arrayLowerBound;
            }
            if (entry.subIndex == 0) {
                if (entry.name.isEmpty()) {
                    entry.name = object.name;
                }
                if (entry.dataType.isEmpty()) {
                    entry.dataType = object.dataType;
                }
            } else {
                for (const OdSubItem& subItem : object.subItems) {
                    if (subItem.subIndex != entry.subIndex) {
                        continue;
                    }
                    if (entry.name.isEmpty()) {
                        entry.name = subItem.name;
                    }
                    if (entry.dataType.isEmpty()) {
                        entry.dataType = subItem.dataType;
                    }
                    break;
                }
            }
        }
        processOffset += mapping.bitLength;
    }
    device.rxPdoBits = rxOffset;
    device.txPdoBits = txOffset;
}

EsiDevice parseDevice(QXmlStreamReader& xml,
                      const VendorInfo& vendor,
                      QVector<ParseDiagnostic>& diagnostics,
                      const QString& source)
{
    EsiDevice result;
    result.sourceFile = source;
    result.vendorId = vendor.id;
    result.vendorName = vendor.name;
    bool nameIsEnglish = false;

    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Type")) {
            quint64 value = 0;
            const QString productCode = xml.attributes().value(QStringLiteral("ProductCode")).toString();
            if (!productCode.isEmpty() && EsiParser::parseUnsigned(productCode, &value)) {
                result.productCode = static_cast<quint32>(value);
            }
            const QString revision = xml.attributes().value(QStringLiteral("RevisionNo")).toString();
            if (!revision.isEmpty() && EsiParser::parseUnsigned(revision, &value)) {
                result.revisionNo = static_cast<quint32>(value);
            }
            result.typeName = elementText(xml);
        } else if (xml.name() == QStringLiteral("Name")) {
            assignLocalizedText(xml, result.name, nameIsEnglish);
        } else if (xml.name() == QStringLiteral("GroupType")) {
            result.groupType = elementText(xml);
        } else if (xml.name() == QStringLiteral("Profile")) {
            parseProfile(xml, result, diagnostics, source);
        } else if (xml.name() == QStringLiteral("RxPdo")) {
            result.pdoMappings.push_back(parsePdo(xml, PdoDirection::Rx, diagnostics, source));
        } else if (xml.name() == QStringLiteral("TxPdo")) {
            result.pdoMappings.push_back(parsePdo(xml, PdoDirection::Tx, diagnostics, source));
        } else if (xml.name() == QStringLiteral("Mailbox")) {
            parseMailbox(xml, result);
        } else {
            xml.skipCurrentElement();
        }
    }

    if (result.name.isEmpty()) {
        result.name = result.typeName;
    }
    enrichPdoEntries(result);
    return result;
}

void parseVendor(QXmlStreamReader& xml,
                 VendorInfo& vendor,
                 QVector<ParseDiagnostic>& diagnostics,
                 const QString& source)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("Id")) {
            quint64 value = 0;
            if (readUnsignedElement(xml, value, diagnostics, source)) {
                vendor.id = static_cast<quint32>(value);
            }
        } else if (xml.name() == QStringLiteral("Name")) {
            assignLocalizedText(xml, vendor.name, vendor.englishNameFound);
        } else {
            xml.skipCurrentElement();
        }
    }
}

} // namespace

bool EsiParseResult::ok() const
{
    for (const ParseDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == ParseDiagnostic::Severity::Error) {
            return false;
        }
    }
    return true;
}

EsiParseResult EsiParser::parseFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        EsiParseResult result;
        result.diagnostics.push_back({ParseDiagnostic::Severity::Error,
                                      filePath,
                                      0,
                                      0,
                                      QStringLiteral("无法打开 ESI 文件：%1").arg(fileErrorDiagnostic(file))});
        return result;
    }
    return parse(&file, QFileInfo(filePath).absoluteFilePath());
}

EsiParseResult EsiParser::parse(QIODevice* device, const QString& sourceName) const
{
    EsiParseResult result;
    if (!device) {
        result.diagnostics.push_back({ParseDiagnostic::Severity::Error,
                                      sourceName,
                                      0,
                                      0,
                                      QStringLiteral("ESI 输入设备为空")});
        return result;
    }
    if (!device->isOpen() && !device->open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.diagnostics.push_back({ParseDiagnostic::Severity::Error,
                                      sourceName,
                                      0,
                                      0,
                                      QStringLiteral("无法打开 ESI 输入设备；原始诊断：%1")
                                          .arg(device->errorString())});
        return result;
    }

    QXmlStreamReader xml(device);
    VendorInfo vendor;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (xml.name() == QStringLiteral("Vendor")) {
            parseVendor(xml, vendor, result.diagnostics, sourceName);
        } else if (xml.name() == QStringLiteral("Device")) {
            result.devices.push_back(parseDevice(xml, vendor, result.diagnostics, sourceName));
        }
    }

    if (xml.hasError()) {
        result.diagnostics.push_back({ParseDiagnostic::Severity::Error,
                                      sourceName,
                                      static_cast<qint64>(xml.lineNumber()),
                                      static_cast<qint64>(xml.columnNumber()),
                                      xmlErrorDiagnostic(xml)});
    } else if (result.devices.isEmpty()) {
        result.diagnostics.push_back({ParseDiagnostic::Severity::Warning,
                                      sourceName,
                                      0,
                                      0,
                                      QStringLiteral("ESI 文件未包含 Device 元素")});
    }
    return result;
}

bool EsiParser::parseUnsigned(const QString& text, quint64* value)
{
    if (!value) {
        return false;
    }
    QString normalized = text.trimmed();
    int base = 10;
    if (normalized.startsWith(QStringLiteral("#x"), Qt::CaseInsensitive)) {
        normalized.remove(0, 2);
        base = 16;
    } else if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        normalized.remove(0, 2);
        base = 16;
    }
    if (normalized.isEmpty()) {
        return false;
    }
    bool ok = false;
    const quint64 parsed = normalized.toULongLong(&ok, base);
    if (ok) {
        *value = parsed;
    }
    return ok;
}

} // namespace explorer

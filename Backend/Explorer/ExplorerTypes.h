#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>

namespace explorer {

enum class PdoDirection {
    Rx,
    Tx
};

enum class AccessMode {
    None,
    ReadOnly,
    WriteOnly,
    ReadWrite
};

enum class EsiMatchKind {
    None,
    ExactIdentity,
    UniqueNormalizedName
};

struct ParseDiagnostic
{
    enum class Severity {
        Warning,
        Error
    };

    Severity severity{Severity::Error};
    QString source;
    qint64 line{0};
    qint64 column{0};
    QString message;
};

struct OdSubItem
{
    quint8 subIndex{0};
    QString name;
    QString dataType;
    int bitSize{0};
    int bitOffset{0};
    AccessMode access{AccessMode::None};
    QString pdoMapping;
    QString defaultValue;
};

struct DataTypeDefinition
{
    QString name;
    QString baseType;
    int bitSize{0};
    int arrayLowerBound{0};
    int arrayElements{0};
    int arrayElementBitSize{0};
    QVector<OdSubItem> subItems;

    bool isArray() const { return arrayElements > 0; }
};

struct ObjectDictionaryEntry
{
    quint16 index{0};
    QString name;
    QString dataType;
    int bitSize{0};
    AccessMode access{AccessMode::None};
    QString pdoMapping;
    quint16 onlineAccessMask{0};
    bool onlineAccessKnown{false};
    QString defaultValue;
    QVector<OdSubItem> subItems;
};

struct PdoEntry
{
    quint16 index{0};
    quint8 subIndex{0};
    QString name;
    QString dataType;
    int bitLength{0};
    qsizetype pdoBitOffset{0};
    qsizetype processBitOffset{0};
    QString arrayName;
    int arrayLowerBound{0};
    int arrayElements{0};
    int arrayElementIndex{-1};
};

struct PdoMapping
{
    PdoDirection direction{PdoDirection::Rx};
    quint16 index{0};
    QString name;
    int syncManager{-1};
    bool fixed{false};
    bool mandatory{false};
    QVector<PdoEntry> entries;
    qsizetype bitLength{0};
};

struct EsiDevice
{
    QString sourceFile;
    quint32 vendorId{0};
    QString vendorName;
    quint32 productCode{0};
    quint32 revisionNo{0};
    QString typeName;
    QString name;
    QString groupType;
    QHash<QString, DataTypeDefinition> dataTypes;
    QVector<ObjectDictionaryEntry> objects;
    QVector<PdoMapping> pdoMappings;
    bool coeSupported{false};
    bool sdoInfoSupported{false};
    bool pdoUploadSupported{false};
    qsizetype rxPdoBits{0};
    qsizetype txPdoBits{0};
};

struct ActivePdoEntry
{
    PdoDirection direction{PdoDirection::Rx};
    quint16 index{0};
    quint8 subIndex{0};
    int bitLength{0};
};

struct OnlineSlaveIdentity
{
    quint16 address{0};
    QString name;
    quint32 vendorId{0};
    quint32 productCode{0};
    quint32 revisionNo{0};
    bool activeMappingKnown{false};
    QVector<ActivePdoEntry> activePdoEntries;
};

struct EsiMatchResult
{
    bool matched{false};
    bool mappingChecked{false};
    bool mappingCompatible{false};
    bool trusted{false};
    EsiMatchKind kind{EsiMatchKind::None};
    EsiDevice device;
    QString reason;
};

struct SlaveSnapshot
{
    quint16 address{0};
    QString name;
    quint32 vendorId{0};
    quint32 productCode{0};
    quint32 revisionNo{0};
    QString serialNumber;
    quint16 state{0};
    QString stateText;
    quint16 alStatusCode{0};
    QString alStatusText;
    int inputBits{0};
    int outputBits{0};
    bool esiMatched{false};
    bool esiTrusted{false};
    QString esiPath;
};

struct PdoVariable
{
    QString stableId;
    quint16 slaveAddress{0};
    PdoDirection direction{PdoDirection::Rx};
    quint16 pdoIndex{0};
    QString pdoName;
    quint16 index{0};
    quint8 subIndex{0};
    QString name;
    QString dataType;
    int bitLength{0};
    qsizetype bitOffset{0};
    QString arrayName;
    int arrayLowerBound{0};
    int arrayElements{0};
    int arrayElementIndex{-1};
    QVariant value;
    QString displayValue;
    bool writable{false};
};

struct PdoMappingItem
{
    QString stableId;
    quint16 slaveAddress{0};
    PdoDirection direction{PdoDirection::Rx};
    quint16 pdoIndex{0};
    QString pdoName;
    int syncManager{-1};
    bool fixed{false};
    bool mandatory{false};
    quint16 index{0};
    quint8 subIndex{0};
    QString name;
    QString dataType;
    int bitLength{0};
    qsizetype pdoBitOffset{0};
    qsizetype processBitOffset{0};
};

struct ObjectDictionaryItem
{
    QString stableId;
    quint16 slaveAddress{0};
    quint16 index{0};
    quint8 subIndex{0};
    QString name;
    QString dataType;
    int bitLength{0};
    AccessMode access{AccessMode::None};
    QString pdoMapping;
    QVariant value;
    QString displayValue;
};

QString normalizedDeviceName(const QString& name);
QString accessModeText(AccessMode access);
AccessMode accessModeFromText(const QString& text);
QString pdoDirectionText(PdoDirection direction);
QString hexValue(quint64 value, int width = 0);
QString makePdoStableId(quint16 slaveAddress,
                        PdoDirection direction,
                        quint16 pdoIndex,
                        quint16 index,
                        quint8 subIndex,
                        qsizetype processBitOffset = 0);
QString makeSdoStableId(quint16 slaveAddress, quint16 index, quint8 subIndex);
QVector<ActivePdoEntry> flattenedPdoSignature(const EsiDevice& device);
bool pdoSignaturesEqual(const QVector<ActivePdoEntry>& lhs,
                        const QVector<ActivePdoEntry>& rhs);

} // namespace explorer

Q_DECLARE_METATYPE(explorer::PdoDirection)
Q_DECLARE_METATYPE(explorer::AccessMode)
Q_DECLARE_METATYPE(explorer::EsiMatchKind)

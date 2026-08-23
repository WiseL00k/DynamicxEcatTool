#include "Backend/Explorer/ExplorerTypes.h"

#include <QRegularExpression>

namespace explorer {

QString normalizedDeviceName(const QString& name)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QString normalized = name.trimmed().toCaseFolded();
    normalized.replace(whitespace, QStringLiteral(" "));
    return normalized;
}

QString accessModeText(AccessMode access)
{
    switch (access) {
    case AccessMode::ReadOnly:
        return QStringLiteral("ro");
    case AccessMode::WriteOnly:
        return QStringLiteral("wo");
    case AccessMode::ReadWrite:
        return QStringLiteral("rw");
    case AccessMode::None:
        return {};
    }
    return {};
}

AccessMode accessModeFromText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("ro") || normalized == QStringLiteral("read")) {
        return AccessMode::ReadOnly;
    }
    if (normalized == QStringLiteral("wo") || normalized == QStringLiteral("write")) {
        return AccessMode::WriteOnly;
    }
    if (normalized == QStringLiteral("rw") || normalized == QStringLiteral("readwrite")
        || normalized == QStringLiteral("read/write")) {
        return AccessMode::ReadWrite;
    }
    return AccessMode::None;
}

QString pdoDirectionText(PdoDirection direction)
{
    return direction == PdoDirection::Rx ? QStringLiteral("RxPDO") : QStringLiteral("TxPDO");
}

QString hexValue(quint64 value, int width)
{
    const QString digits =
        QString::number(value, 16).rightJustified(width, QLatin1Char('0')).toUpper();
    return QStringLiteral("0x") + digits;
}

QString makePdoStableId(quint16 slaveAddress,
                        PdoDirection direction,
                        quint16 pdoIndex,
                        quint16 index,
                        quint8 subIndex,
                        qsizetype processBitOffset)
{
    return QStringLiteral("pdo:%1:%2:%3:%4:%5:%6")
        .arg(slaveAddress)
        .arg(direction == PdoDirection::Rx ? QStringLiteral("rx") : QStringLiteral("tx"))
        .arg(pdoIndex, 4, 16, QLatin1Char('0'))
        .arg(index, 4, 16, QLatin1Char('0'))
        .arg(subIndex, 2, 16, QLatin1Char('0'))
        .arg(processBitOffset);
}

QString makeSdoStableId(quint16 slaveAddress, quint16 index, quint8 subIndex)
{
    return QStringLiteral("sdo:%1:%2:%3")
        .arg(slaveAddress)
        .arg(index, 4, 16, QLatin1Char('0'))
        .arg(subIndex, 2, 16, QLatin1Char('0'));
}

QVector<ActivePdoEntry> flattenedPdoSignature(const EsiDevice& device)
{
    QVector<ActivePdoEntry> result;
    for (const PdoMapping& mapping : device.pdoMappings) {
        for (const PdoEntry& entry : mapping.entries) {
            result.push_back({mapping.direction, entry.index, entry.subIndex, entry.bitLength});
        }
    }
    return result;
}

bool pdoSignaturesEqual(const QVector<ActivePdoEntry>& lhs,
                        const QVector<ActivePdoEntry>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (qsizetype i = 0; i < lhs.size(); ++i) {
        const ActivePdoEntry& left = lhs.at(i);
        const ActivePdoEntry& right = rhs.at(i);
        if (left.direction != right.direction || left.index != right.index
            || left.subIndex != right.subIndex || left.bitLength != right.bitLength) {
            return false;
        }
    }
    return true;
}

} // namespace explorer

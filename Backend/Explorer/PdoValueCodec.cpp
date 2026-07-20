#include "Backend/Explorer/PdoValueCodec.h"

#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace explorer {
namespace {

QString normalizedType(const QString& dataType)
{
    QString result = dataType.trimmed().toUpper();
    result.remove(QLatin1Char(' '));
    result.remove(QLatin1Char('_'));
    return result;
}

bool rangeIsValid(const QByteArray& data, qsizetype bitOffset, int bitLength)
{
    if (bitOffset < 0 || bitLength <= 0) {
        return false;
    }
    const qsizetype availableBits = data.size() * 8;
    return bitOffset <= availableBits && bitLength <= availableBits - bitOffset;
}

quint64 readBits(const QByteArray& data, qsizetype bitOffset, int bitLength)
{
    quint64 value = 0;
    for (int bit = 0; bit < bitLength; ++bit) {
        const qsizetype sourceBit = bitOffset + bit;
        const quint8 sourceByte = static_cast<quint8>(data.at(sourceBit / 8));
        if ((sourceByte & (quint8(1) << (sourceBit % 8))) != 0) {
            value |= quint64(1) << bit;
        }
    }
    return value;
}

void writeBits(QByteArray& data, qsizetype bitOffset, int bitLength, quint64 value)
{
    for (int bit = 0; bit < bitLength; ++bit) {
        const qsizetype targetBit = bitOffset + bit;
        quint8 targetByte = static_cast<quint8>(data.at(targetBit / 8));
        const quint8 mask = quint8(1) << (targetBit % 8);
        if ((value & (quint64(1) << bit)) != 0) {
            targetByte |= mask;
        } else {
            targetByte &= static_cast<quint8>(~mask);
        }
        data[targetBit / 8] = static_cast<char>(targetByte);
    }
}

QByteArray readRawBits(const QByteArray& data, qsizetype bitOffset, int bitLength)
{
    QByteArray result((bitLength + 7) / 8, '\0');
    for (int bit = 0; bit < bitLength; ++bit) {
        const qsizetype sourceBit = bitOffset + bit;
        const quint8 sourceByte = static_cast<quint8>(data.at(sourceBit / 8));
        if ((sourceByte & (quint8(1) << (sourceBit % 8))) != 0) {
            quint8 targetByte = static_cast<quint8>(result.at(bit / 8));
            targetByte |= quint8(1) << (bit % 8);
            result[bit / 8] = static_cast<char>(targetByte);
        }
    }
    return result;
}

void writeRawBits(QByteArray& data,
                  qsizetype bitOffset,
                  int bitLength,
                  const QByteArray& rawValue)
{
    for (int bit = 0; bit < bitLength; ++bit) {
        const quint8 sourceByte = static_cast<quint8>(rawValue.at(bit / 8));
        const bool set = (sourceByte & (quint8(1) << (bit % 8))) != 0;
        const qsizetype targetBit = bitOffset + bit;
        quint8 targetByte = static_cast<quint8>(data.at(targetBit / 8));
        const quint8 mask = quint8(1) << (targetBit % 8);
        targetByte = set ? static_cast<quint8>(targetByte | mask)
                         : static_cast<quint8>(targetByte & ~mask);
        data[targetBit / 8] = static_cast<char>(targetByte);
    }
}

bool parseBoolean(const QVariant& value, bool* result)
{
    if (!result) {
        return false;
    }
    if (value.metaType().id() == QMetaType::Bool) {
        *result = value.toBool();
        return true;
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")
        || text == QStringLiteral("on") || text == QStringLiteral("yes")) {
        *result = true;
        return true;
    }
    if (text == QStringLiteral("false") || text == QStringLiteral("0")
        || text == QStringLiteral("off") || text == QStringLiteral("no")) {
        *result = false;
        return true;
    }
    return false;
}

bool parseSigned(const QVariant& value, qint64* result)
{
    QString text = value.toString().trimmed();
    bool ok = false;
    int base = 10;
    bool negative = false;
    if (text.startsWith(QLatin1Char('-'))) {
        negative = true;
        text.remove(0, 1);
    } else if (text.startsWith(QLatin1Char('+'))) {
        text.remove(0, 1);
    }
    if (text.startsWith(QStringLiteral("#x"), Qt::CaseInsensitive)
        || text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    }
    const quint64 magnitude = text.toULongLong(&ok, base);
    if (!ok) {
        return false;
    }
    if (negative) {
        const quint64 minimumMagnitude = quint64(std::numeric_limits<qint64>::max()) + 1;
        if (magnitude > minimumMagnitude) {
            return false;
        }
        *result = magnitude == minimumMagnitude ? std::numeric_limits<qint64>::min()
                                                : -static_cast<qint64>(magnitude);
    } else {
        if (magnitude > quint64(std::numeric_limits<qint64>::max())) {
            return false;
        }
        *result = static_cast<qint64>(magnitude);
    }
    return true;
}

bool parseUnsigned(const QVariant& value, quint64* result)
{
    QString text = value.toString().trimmed();
    if (text.startsWith(QLatin1Char('-'))) {
        return false;
    }
    if (text.startsWith(QLatin1Char('+'))) {
        text.remove(0, 1);
    }
    int base = 10;
    if (text.startsWith(QStringLiteral("#x"), Qt::CaseInsensitive)
        || text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        text.remove(0, 2);
        base = 16;
    }
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok, base);
    if (ok) {
        *result = parsed;
    }
    return ok;
}

bool signedValueFits(qint64 value, int bitLength)
{
    if (bitLength >= 64) {
        return true;
    }
    const qint64 minimum = -(qint64(1) << (bitLength - 1));
    const qint64 maximum = (qint64(1) << (bitLength - 1)) - 1;
    return value >= minimum && value <= maximum;
}

bool unsignedValueFits(quint64 value, int bitLength)
{
    return bitLength >= 64 || value <= ((quint64(1) << bitLength) - 1);
}

QByteArray parseRaw(const QVariant& value, bool* ok)
{
    if (value.metaType().id() == QMetaType::QByteArray) {
        *ok = true;
        return value.toByteArray();
    }

    QString text = value.toString().trimmed();
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
        || text.startsWith(QStringLiteral("#x"), Qt::CaseInsensitive)) {
        text.remove(0, 2);
    }
    text.remove(QRegularExpression(QStringLiteral("[\\s:_-]")));
    static const QRegularExpression hexPattern(QStringLiteral("^[0-9A-Fa-f]*$"));
    if (text.size() % 2 != 0 || !hexPattern.match(text).hasMatch()) {
        *ok = false;
        return {};
    }
    *ok = true;
    return QByteArray::fromHex(text.toLatin1());
}

QString rawDisplay(const QByteArray& value)
{
    return QString::fromLatin1(value.toHex(' ').toUpper());
}

} // namespace

PdoDecodeResult PdoValueCodec::decode(const QByteArray& processImage,
                                      qsizetype bitOffset,
                                      int bitLength,
                                      const QString& dataType)
{
    PdoDecodeResult result;
    if (!rangeIsValid(processImage, bitOffset, bitLength)) {
        result.error = QStringLiteral("PDO field is outside the process image");
        return result;
    }

    if (isBooleanType(dataType)) {
        const bool value = readBits(processImage, bitOffset, 1) != 0;
        result.ok = true;
        result.value = value;
        result.displayValue = value ? QStringLiteral("true") : QStringLiteral("false");
        return result;
    }

    if (isSignedIntegerType(dataType) || isUnsignedIntegerType(dataType)) {
        if (bitLength > 64) {
            result.error = QStringLiteral("Integer PDO fields wider than 64 bits are not supported");
            return result;
        }
        const quint64 raw = readBits(processImage, bitOffset, bitLength);
        result.ok = true;
        if (isSignedIntegerType(dataType)) {
            qint64 value = 0;
            if (bitLength == 64) {
                std::memcpy(&value, &raw, sizeof(value));
            } else if ((raw & (quint64(1) << (bitLength - 1))) != 0) {
                value = static_cast<qint64>(raw | (~quint64(0) << bitLength));
            } else {
                value = static_cast<qint64>(raw);
            }
            result.value = value;
            result.displayValue = QString::number(value);
        } else {
            result.value = raw;
            result.displayValue = QString::number(raw);
        }
        return result;
    }

    if (isFloatingPointType(dataType)) {
        const QString type = normalizedType(dataType);
        if (type == QStringLiteral("REAL") || type == QStringLiteral("REAL32")
            || type == QStringLiteral("FLOAT32")) {
            if (bitLength != 32) {
                result.error = QStringLiteral("REAL requires a 32-bit PDO field");
                return result;
            }
            const quint32 raw = static_cast<quint32>(readBits(processImage, bitOffset, bitLength));
            float value = 0.0F;
            std::memcpy(&value, &raw, sizeof(value));
            result.ok = true;
            result.value = value;
            result.displayValue = QLocale::c().toString(value, 'g', 9);
            return result;
        }
        if (bitLength != 64) {
            result.error = QStringLiteral("LREAL requires a 64-bit PDO field");
            return result;
        }
        const quint64 raw = readBits(processImage, bitOffset, bitLength);
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        result.ok = true;
        result.value = value;
        result.displayValue = QLocale::c().toString(value, 'g', 17);
        return result;
    }

    if (isStringType(dataType)) {
        if (bitLength % 8 != 0) {
            result.error = QStringLiteral("String fields must be byte-aligned in length");
            return result;
        }
        QByteArray raw = readRawBits(processImage, bitOffset, bitLength);
        const qsizetype terminator = raw.indexOf('\0');
        if (terminator >= 0) {
            raw.truncate(terminator);
        }
        result.ok = true;
        result.value = QString::fromLatin1(raw);
        result.displayValue = result.value.toString();
        return result;
    }

    const QByteArray raw = readRawBits(processImage, bitOffset, bitLength);
    result.ok = true;
    result.value = raw;
    result.displayValue = rawDisplay(raw);
    return result;
}

PdoEncodeResult PdoValueCodec::encode(const QVariant& value,
                                      const QString& dataType,
                                      int bitLength,
                                      QByteArray* processImage,
                                      qsizetype bitOffset)
{
    PdoEncodeResult result;
    if (!processImage || !rangeIsValid(*processImage, bitOffset, bitLength)) {
        result.error = QStringLiteral("PDO field is outside the process image");
        return result;
    }

    QByteArray updated = *processImage;
    if (isBooleanType(dataType)) {
        bool parsed = false;
        if (!parseBoolean(value, &parsed)) {
            result.error = QStringLiteral("Expected a boolean value");
            return result;
        }
        QByteArray raw((bitLength + 7) / 8, '\0');
        raw[0] = parsed ? '\1' : '\0';
        writeRawBits(updated, bitOffset, bitLength, raw);
    } else if (isSignedIntegerType(dataType)) {
        if (bitLength > 64) {
            result.error = QStringLiteral("Integer PDO fields wider than 64 bits are not supported");
            return result;
        }
        qint64 parsed = 0;
        if (!parseSigned(value, &parsed)) {
            result.error = QStringLiteral("Expected a signed integer value");
            return result;
        }
        if (!signedValueFits(parsed, bitLength)) {
            result.error = QStringLiteral("Signed value is outside the %1-bit range").arg(bitLength);
            return result;
        }
        quint64 raw = 0;
        std::memcpy(&raw, &parsed, sizeof(raw));
        writeBits(updated, bitOffset, bitLength, raw);
    } else if (isUnsignedIntegerType(dataType)) {
        if (bitLength > 64) {
            result.error = QStringLiteral("Integer PDO fields wider than 64 bits are not supported");
            return result;
        }
        quint64 parsed = 0;
        if (!parseUnsigned(value, &parsed)) {
            result.error = QStringLiteral("Expected an unsigned integer value");
            return result;
        }
        if (!unsignedValueFits(parsed, bitLength)) {
            result.error = QStringLiteral("Unsigned value is outside the %1-bit range").arg(bitLength);
            return result;
        }
        writeBits(updated, bitOffset, bitLength, parsed);
    } else if (isFloatingPointType(dataType)) {
        bool ok = false;
        const double parsed = QLocale::c().toDouble(value.toString().trimmed(), &ok);
        if (!ok || !std::isfinite(parsed)) {
            result.error = QStringLiteral("Expected a finite floating-point value");
            return result;
        }
        const QString type = normalizedType(dataType);
        if (type == QStringLiteral("REAL") || type == QStringLiteral("REAL32")
            || type == QStringLiteral("FLOAT32")) {
            if (bitLength != 32) {
                result.error = QStringLiteral("REAL requires a 32-bit PDO field");
                return result;
            }
            const float narrowed = static_cast<float>(parsed);
            if (!std::isfinite(narrowed)) {
                result.error = QStringLiteral("Value is outside the REAL range");
                return result;
            }
            quint32 raw = 0;
            std::memcpy(&raw, &narrowed, sizeof(raw));
            writeBits(updated, bitOffset, bitLength, raw);
        } else {
            if (bitLength != 64) {
                result.error = QStringLiteral("LREAL requires a 64-bit PDO field");
                return result;
            }
            quint64 raw = 0;
            std::memcpy(&raw, &parsed, sizeof(raw));
            writeBits(updated, bitOffset, bitLength, raw);
        }
    } else if (isStringType(dataType)) {
        if (bitLength % 8 != 0) {
            result.error = QStringLiteral("String fields must be byte-aligned in length");
            return result;
        }
        const QString text = value.toString();
        const QByteArray rawText = text.toLatin1();
        if (QString::fromLatin1(rawText) != text) {
            result.error = QStringLiteral("VISIBLE_STRING accepts Latin-1 characters only");
            return result;
        }
        const qsizetype fieldBytes = bitLength / 8;
        if (rawText.size() > fieldBytes) {
            result.error = QStringLiteral("String value exceeds the %1-byte field").arg(fieldBytes);
            return result;
        }
        QByteArray raw(fieldBytes, '\0');
        std::copy(rawText.cbegin(), rawText.cend(), raw.begin());
        writeRawBits(updated, bitOffset, bitLength, raw);
    } else {
        bool ok = false;
        const QByteArray raw = parseRaw(value, &ok);
        const qsizetype expectedBytes = (bitLength + 7) / 8;
        if (!ok) {
            result.error = QStringLiteral("Expected an even-length hexadecimal byte string");
            return result;
        }
        if (raw.size() != expectedBytes) {
            result.error = QStringLiteral("Raw value requires exactly %1 bytes").arg(expectedBytes);
            return result;
        }
        if (bitLength % 8 != 0) {
            const quint8 unusedMask = static_cast<quint8>(0xffU << (bitLength % 8));
            if ((static_cast<quint8>(raw.back()) & unusedMask) != 0) {
                result.error = QStringLiteral("Raw value sets bits outside the field width");
                return result;
            }
        }
        writeRawBits(updated, bitOffset, bitLength, raw);
    }

    *processImage = std::move(updated);
    result.ok = true;
    return result;
}

bool PdoValueCodec::isBooleanType(const QString& dataType)
{
    const QString type = normalizedType(dataType);
    return type == QStringLiteral("BOOL") || type == QStringLiteral("BOOLEAN")
        || type == QStringLiteral("BIT");
}

bool PdoValueCodec::isSignedIntegerType(const QString& dataType)
{
    const QString type = normalizedType(dataType);
    return type == QStringLiteral("SINT") || type == QStringLiteral("INT")
        || type == QStringLiteral("DINT") || type == QStringLiteral("LINT")
        || type == QStringLiteral("INTEGER8") || type == QStringLiteral("INTEGER16")
        || type == QStringLiteral("INTEGER24") || type == QStringLiteral("INTEGER32")
        || type == QStringLiteral("INTEGER64");
}

bool PdoValueCodec::isUnsignedIntegerType(const QString& dataType)
{
    const QString type = normalizedType(dataType);
    return type == QStringLiteral("USINT") || type == QStringLiteral("UINT")
        || type == QStringLiteral("UDINT") || type == QStringLiteral("ULINT")
        || type == QStringLiteral("BYTE") || type == QStringLiteral("WORD")
        || type == QStringLiteral("DWORD") || type == QStringLiteral("LWORD")
        || type == QStringLiteral("UNSIGNED8") || type == QStringLiteral("UNSIGNED16")
        || type == QStringLiteral("UNSIGNED24") || type == QStringLiteral("UNSIGNED32")
        || type == QStringLiteral("UNSIGNED64");
}

bool PdoValueCodec::isFloatingPointType(const QString& dataType)
{
    const QString type = normalizedType(dataType);
    return type == QStringLiteral("REAL") || type == QStringLiteral("LREAL")
        || type == QStringLiteral("REAL32") || type == QStringLiteral("REAL64")
        || type == QStringLiteral("FLOAT32") || type == QStringLiteral("FLOAT64");
}

bool PdoValueCodec::isStringType(const QString& dataType)
{
    const QString type = normalizedType(dataType);
    return type == QStringLiteral("VISIBLESTRING") || type == QStringLiteral("STRING")
        || type.startsWith(QStringLiteral("STRING("))
        || type == QStringLiteral("CHARARRAY");
}

bool PdoValueCodec::isRawType(const QString& dataType)
{
    return !isBooleanType(dataType) && !isSignedIntegerType(dataType)
        && !isUnsignedIntegerType(dataType) && !isFloatingPointType(dataType)
        && !isStringType(dataType);
}

} // namespace explorer

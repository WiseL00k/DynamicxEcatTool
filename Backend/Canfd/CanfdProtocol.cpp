#include "CanfdProtocol.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace Backend::canfd {

QString hex(quint64 value, int width)
{
    return QString::number(value, 16).rightJustified(width, QLatin1Char('0')).toUpper();
}

QString bytesHex(const quint8* bytes, int length)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes), length).toHex(' ').toUpper());
}

quint32 readLe(const quint8* data, int count)
{
    quint32 value = 0;
    for (int i = 0; i < count; ++i) value |= quint32(data[i]) << (i * 8);
    return value;
}

std::vector<quint8> writeLe(quint32 value, int count)
{
    std::vector<quint8> bytes(count);
    for (int i = 0; i < count; ++i) bytes[i] = quint8(value >> (8 * i));
    return bytes;
}

QVariantMap ChannelConfig::toMap() const
{
    static const char* labels[] = {"已应用", "待应用", "应用中", "错误"};
    return {{"slave", slave}, {"channel", channel}, {"enabled", enabled},
            {"format", format}, {"nominalBitrate", nominalBitrate}, {"dataBitrate", dataBitrate},
            {"options", options}, {"status", status}, {"error", error},
            {"errorHex", hex(error, 8)}, {"verified", verified},
            {"dirty", !verified || status != 0},
            {"statusText", verified ? QString::fromUtf8(status < 4 ? labels[status] : "未知") : QStringLiteral("未确认")}};
}

bool validConfig(const ChannelConfig& c, QString& error)
{
    const std::array<quint32, 5> nominal{100000, 125000, 250000, 500000, 1000000};
    if (c.slave < 1 || c.channel < 0 || c.channel > 1 || c.format < 0 || c.format > 2
        || (c.options & ~7U) != 0
        || std::find(nominal.begin(), nominal.end(), c.nominalBitrate) == nominal.end()
        || (c.dataBitrate != 2500000 && c.dataBitrate != 5000000)) {
        error = QStringLiteral("CAN 配置超出固件支持范围");
        return false;
    }
    return true;
}

bool parseConfig(int slave, const QVariantMap& map, ChannelConfig& result, QString& error)
{
    for (const auto* key : {"channel", "enabled", "format", "nominalBitrate", "dataBitrate", "options"}) {
        if (!map.contains(QLatin1String(key))) {
            error = QStringLiteral("CAN 配置缺少字段 %1").arg(QLatin1String(key));
            return false;
        }
    }
    auto integer = [&](const char* key, qint64 min, qint64 max, qint64& out) {
        bool ok = false;
        const double d = map.value(QLatin1String(key)).toDouble(&ok);
        if (!ok || !std::isfinite(d) || d < min || d > max || d != qint64(d)) return false;
        out = qint64(d);
        return true;
    };
    qint64 channel, format, nominal, data, options, enabled;
    if (!integer("channel", 0, 1, channel) || !integer("format", 0, 2, format)
        || !integer("nominalBitrate", 0, UINT32_MAX, nominal)
        || !integer("dataBitrate", 0, UINT32_MAX, data)
        || !integer("options", 0, 7, options) || !integer("enabled", 0, 1, enabled)) {
        error = QStringLiteral("CAN 配置字段类型或范围无效");
        return false;
    }
    result.slave = slave; result.channel = int(channel); result.enabled = enabled != 0;
    result.format = int(format); result.nominalBitrate = quint32(nominal);
    result.dataBitrate = quint32(data); result.options = quint16(options);
    return validConfig(result, error);
}

bool parseFrame(int slave, int channel, const QString& id, int dlc,
                const QString& data, Frame& result, QString& error)
{
    QString idText = id.trimmed();
    if (idText.startsWith("0x", Qt::CaseInsensitive)) idText.remove(0, 2);
    static const QRegularExpression idPattern(QStringLiteral("^[0-9a-fA-F]{1,3}$"));
    bool ok = false;
    const uint numericId = idText.toUInt(&ok, 16);
    if (slave < 1 || channel < 0 || channel > 1 || !ok || !idPattern.match(idText).hasMatch()
        || numericId > 0x7ff || dlc < 0 || dlc > 8) {
        error = QStringLiteral("帧参数无效：通道为 0/1，标准 ID 为 000–7FF，DLC 为 0–8");
        return false;
    }
    const QString normalized = data.trimmed();
    const QStringList tokens = normalized.isEmpty() ? QStringList{} : normalized.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    static const QRegularExpression bytePattern(QStringLiteral("^[0-9a-fA-F]{2}$"));
    if (tokens.size() != dlc) {
        error = QStringLiteral("数据字节数必须等于 DLC；使用空格分隔两位十六进制字节");
        return false;
    }
    result = {}; result.slave = slave; result.channel = channel; result.id = quint16(numericId); result.dlc = dlc;
    for (int i = 0; i < tokens.size(); ++i) {
        if (!bytePattern.match(tokens[i]).hasMatch()) {
            error = QStringLiteral("每个数据字节必须为两位十六进制数"); return false;
        }
        result.data[i] = quint8(tokens[i].toUInt(nullptr, 16));
    }
    return true;
}

bool validateScan(const soem_interface::BusScanResult& scan, int expectedCount, QString& error)
{
    using namespace soem_interface;
    if (!scan.success || !scan.mappingReady || !scan.activePdoComplete) {
        error = scan.error.empty() ? QStringLiteral("无法获得完整在线 PDO 映射") : QString::fromStdString(scan.error);
        return false;
    }
    if (scan.slaveCount != expectedCount || int(scan.slaves.size()) != expectedCount) {
        error = QStringLiteral("预期 %1 个从站，实际发现 %2 个；请断开并调整配置").arg(expectedCount).arg(scan.slaveCount);
        return false;
    }
    for (const auto& slave : scan.slaves) {
        if (slave.vendorId != 0 || slave.productCode != 0xAB125 || slave.revision != 3
            || slave.outputBits != ImageBytes * 8 || slave.inputBits != ImageBytes * 8) {
            error = QStringLiteral("从站 %1 的身份或 PDO 长度与 ECAT2CANFD 协议不匹配").arg(slave.position);
            return false;
        }
        for (auto direction : {PdoDirection::Rx, PdoDirection::Tx}) {
            std::vector<ActivePdoEntry> entries;
            for (const auto& e : scan.activePdos) if (e.slave == slave.position && e.direction == direction) entries.push_back(e);
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.bitOffset < b.bitOffset; });
            struct Expected { uint16_t index; uint8_t sub; uint16_t bits; uint32_t offset; };
            std::vector<Expected> expected;
            uint32_t offset = 0;
            auto add = [&](uint16_t index, uint8_t sub, uint16_t bits) { expected.push_back({index, sub, bits, offset}); offset += bits; };
            if (direction == PdoDirection::Rx) add(0x6040, 0, 32);
            const uint16_t base = direction == PdoDirection::Rx ? 0x7000 : 0x6000;
            for (int field = 0; field < 4; ++field)
                for (int slot = 1; slot <= SlotsPerChannel; ++slot) add(base + field, quint8(slot), field < 2 ? 16 : 64);
            add(direction == PdoDirection::Rx ? 0x7005 : 0x600C, 0, 8);
            if (direction == PdoDirection::Tx) add(0x6041, 0, 32);
            bool matches = entries.size() == expected.size();
            for (size_t i = 0; matches && i < expected.size(); ++i) {
                const auto& e = entries[i]; const auto& x = expected[i];
                matches = e.index == x.index && e.subindex == x.sub && e.bitLength == x.bits && e.bitOffset == x.offset;
            }
            if (!matches) {
                error = QStringLiteral("从站 %1 的 %2PDO 映射与固件协议不一致").arg(slave.position).arg(direction == PdoDirection::Rx ? "Rx" : "Tx");
                return false;
            }
        }
    }
    return true;
}

void encodeFrame(std::vector<uint8_t>& image, int slot, const Frame& frame)
{
    if (image.size() != ImageBytes || slot < 0 || slot >= SlotsPerChannel || frame.channel < 0 || frame.channel > 1) return;
    const int infoOffset = 4 + frame.channel * 18 + slot * 2;
    const int dataOffset = 40 + frame.channel * 72 + slot * 8;
    const quint16 info = quint16(frame.id | ValidMask | (frame.dlc << 12));
    image[infoOffset] = quint8(info); image[infoOffset + 1] = quint8(info >> 8);
    std::copy(frame.data.begin(), frame.data.end(), image.begin() + dataOffset);
}

QVariantList decodeRows(const std::vector<uint8_t>& image, bool outputs)
{
    QVariantList rows;
    if (image.size() != ImageBytes) return rows;
    const int prefix = outputs ? 4 : 0;
    for (int channel = 0; channel < 2; ++channel) for (int slot = 0; slot < 9; ++slot) {
        const quint16 info = quint16(readLe(image.data() + prefix + channel * 18 + slot * 2, 2));
        rows.append(QVariantMap{{"channel", channel}, {"slot", slot}, {"valid", bool(info & ValidMask)},
                               {"idHex", hex(info & 0x7ff, 3)}, {"dlc", int(info >> 12)},
                               {"dataHex", bytesHex(image.data() + prefix + 36 + channel * 72 + slot * 8, 8)},
                               {"frameInfoHex", hex(info, 4)}});
    }
    return rows;
}

std::vector<Frame> decodeValidFrames(int slave, const std::vector<uint8_t>& input)
{
    std::vector<Frame> result;
    if (input.size() != ImageBytes) return result;
    for (int channel = 0; channel < 2; ++channel) for (int slot = 0; slot < 9; ++slot) {
        const quint16 info = quint16(readLe(input.data() + channel * 18 + slot * 2, 2));
        if (!(info & ValidMask) || (info >> 12) > 8) continue;
        Frame f; f.slave = slave; f.channel = channel; f.id = info & 0x7ff; f.dlc = info >> 12;
        std::copy_n(input.data() + 36 + channel * 72 + slot * 8, 8, f.data.begin());
        result.push_back(f);
    }
    return result;
}

QVariantMap decodeImage(const soem_interface::SlaveProcessImage& image)
{
    if (image.inputs.size() != ImageBytes || image.outputs.size() != ImageBytes) return {};
    return {{"slave", image.position}, {"rxRows", decodeRows(image.outputs, true)}, {"txRows", decodeRows(image.inputs, false)},
            {"rxControlwordHex", hex(readLe(image.outputs.data(), 4), 8)},
            {"txStatuswordHex", hex(readLe(image.inputs.data() + 181, 4), 8)},
            {"digitalOutputsHex", hex(image.outputs[184], 2)}, {"digitalInputsHex", hex(image.inputs[180], 2)},
            {"rxHex", bytesHex(image.outputs.data(), ImageBytes)}, {"txHex", bytesHex(image.inputs.data(), ImageBytes)}};
}

} // namespace Backend::canfd

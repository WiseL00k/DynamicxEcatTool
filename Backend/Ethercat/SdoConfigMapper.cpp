#include "SdoConfigMapper.h"

#include <QVariantMap>

namespace Backend {
namespace {

bool readUInt(
    const QVariantMap& map,
    const QString& key,
    uint minValue,
    uint maxValue,
    uint& value,
    QString& errorMessage)
{
    if (!map.contains(key)) {
        errorMessage = QStringLiteral("SDO缺少字段: %1").arg(key);
        return false;
    }

    bool ok = false;
    const uint parsed = map.value(key).toUInt(&ok);
    if (!ok || parsed < minValue || parsed > maxValue) {
        errorMessage = QStringLiteral("SDO字段范围错误: %1").arg(key);
        return false;
    }

    value = parsed;
    return true;
}

} // namespace

SdoConfigMappingResult mapSdoConfigs(const QVariantList& list)
{
    SdoConfigMappingResult result;
    result.configs.reserve(static_cast<size_t>(list.size()));

    for (int i = 0; i < list.size(); ++i) {
        if (!list[i].canConvert<QVariantMap>()) {
            result.errorMessage = QStringLiteral("SDO配置第%1项格式错误").arg(i + 1);
            return result;
        }

        const QVariantMap map = list[i].toMap();
        soem_interface::SDOConfig config;

        uint parsed = 0;
        if (!readUInt(map, QStringLiteral("slave"), 1, 65535, parsed, result.errorMessage)) {
            return result;
        }
        config.slave = static_cast<uint16_t>(parsed);

        if (!readUInt(map, QStringLiteral("index"), 1, 65535, parsed, result.errorMessage)) {
            return result;
        }
        config.index = static_cast<uint16_t>(parsed);

        if (!readUInt(map, QStringLiteral("subindex"), 0, 255, parsed, result.errorMessage)) {
            return result;
        }
        config.subindex = static_cast<uint8_t>(parsed);

        if (!map.contains(QStringLiteral("type"))) {
            result.errorMessage = QStringLiteral("SDO缺少字段: type");
            return result;
        }

        const QString type = map.value(QStringLiteral("type")).toString().toLower();
        if (type == QStringLiteral("write")) {
            config.type = soem_interface::SDOType::WRITE;
        } else if (type == QStringLiteral("read")) {
            config.type = soem_interface::SDOType::READ;
        } else {
            result.errorMessage = QStringLiteral("SDO type必须为read或write");
            return result;
        }

        if (config.type == soem_interface::SDOType::WRITE) {
            if (!map.contains(QStringLiteral("data"))) {
                result.errorMessage = QStringLiteral("SDO写配置缺少data字段");
                return result;
            }

            const QVariantList bytes = map.value(QStringLiteral("data")).toList();
            if (bytes.isEmpty()) {
                result.errorMessage = QStringLiteral("SDO写配置data不能为空");
                return result;
            }

            config.data.reserve(static_cast<size_t>(bytes.size()));
            for (int byteIndex = 0; byteIndex < bytes.size(); ++byteIndex) {
                bool ok = false;
                const uint byte = bytes[byteIndex].toUInt(&ok);
                if (!ok || byte > 255U) {
                    result.errorMessage = QStringLiteral("SDO data字节范围错误");
                    return result;
                }
                config.data.push_back(static_cast<uint8_t>(byte));
            }
        } else {
            if (!readUInt(map, QStringLiteral("expected_size"), 1, 1024, parsed, result.errorMessage)) {
                return result;
            }
            config.expected_size = static_cast<int>(parsed);
        }

        result.configs.push_back(std::move(config));
    }

    result.ok = true;
    return result;
}

std::vector<soem_interface::SDOConfig> toSdoConfigs(const QVariantList& list)
{
    return mapSdoConfigs(list).configs;
}

} // namespace Backend
#include <utility>



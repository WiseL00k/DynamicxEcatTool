#include "Backend/Explorer/EsiRepository.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <utility>

namespace explorer {
namespace {

QString signatureMismatchReason(const QVector<ActivePdoEntry>& expected,
                                const QVector<ActivePdoEntry>& actual)
{
    if (expected.size() != actual.size()) {
        return QStringLiteral("当前活动 PDO 有 %1 个条目，ESI 声明了 %2 个")
            .arg(actual.size())
            .arg(expected.size());
    }

    for (qsizetype i = 0; i < expected.size(); ++i) {
        const ActivePdoEntry& esiEntry = expected.at(i);
        const ActivePdoEntry& activeEntry = actual.at(i);
        if (esiEntry.direction == activeEntry.direction && esiEntry.index == activeEntry.index
            && esiEntry.subIndex == activeEntry.subIndex
            && esiEntry.bitLength == activeEntry.bitLength) {
            continue;
        }
        return QStringLiteral("当前活动 PDO 条目 %1 与 ESI 不匹配（ESI：%2 %3:%4，%5 位）")
            .arg(i)
            .arg(pdoDirectionText(esiEntry.direction))
            .arg(hexValue(esiEntry.index, 4))
            .arg(hexValue(esiEntry.subIndex, 2))
            .arg(esiEntry.bitLength);
    }
    return {};
}

} // namespace

bool EsiRepositoryIndexResult::ok() const
{
    for (const ParseDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == ParseDiagnostic::Severity::Error) {
            return false;
        }
    }
    return true;
}

EsiRepository::EsiRepository(EsiParser parser)
    : parser_(std::move(parser))
{}

EsiRepositoryIndexResult EsiRepository::indexDirectory(const QString& directoryPath)
{
    clear();
    directory_ = QDir(directoryPath).absolutePath();

    EsiRepositoryIndexResult result;
    result.directory = directory_;
    const QFileInfo directoryInfo(directory_);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        diagnostics_.push_back({ParseDiagnostic::Severity::Error,
                                directory_,
                                0,
                                0,
                                QStringLiteral("ESI 目录不存在或路径不是文件夹")});
        result.diagnostics = diagnostics_;
        return result;
    }

    QStringList files;
    QDirIterator iterator(directory_,
                          {QStringLiteral("*.xml")},
                          QDir::Files | QDir::Readable,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        files.push_back(QFileInfo(iterator.next()).absoluteFilePath());
    }
    files.sort(Qt::CaseInsensitive);

    QSet<QString> indexedPaths;
    for (const QString& filePath : std::as_const(files)) {
        QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            canonicalPath = QFileInfo(filePath).absoluteFilePath();
        }
        const QString pathKey = canonicalPath.toCaseFolded();
        if (indexedPaths.contains(pathKey)) {
            continue;
        }
        indexedPaths.insert(pathKey);
        ++result.filesScanned;

        EsiParseResult parsed = parser_.parseFile(canonicalPath);
        diagnostics_ += parsed.diagnostics;
        if (!parsed.ok()) {
            continue;
        }
        for (EsiDevice& device : parsed.devices) {
            device.sourceFile = canonicalPath;
            devices_.push_back(std::move(device));
        }
    }

    result.devicesIndexed = devices_.size();
    result.diagnostics = diagnostics_;
    return result;
}

void EsiRepository::clear()
{
    directory_.clear();
    devices_.clear();
    diagnostics_.clear();
}

const QVector<EsiDevice>& EsiRepository::devices() const
{
    return devices_;
}

const QVector<ParseDiagnostic>& EsiRepository::diagnostics() const
{
    return diagnostics_;
}

QString EsiRepository::directory() const
{
    return directory_;
}

EsiMatchResult EsiRepository::match(const OnlineSlaveIdentity& slave) const
{
    QVector<const EsiDevice*> exactCandidates;
    for (const EsiDevice& device : devices_) {
        if (device.vendorId == slave.vendorId && device.productCode == slave.productCode
            && device.revisionNo == slave.revisionNo) {
            exactCandidates.push_back(&device);
        }
    }

    if (exactCandidates.size() == 1) {
        return resultForCandidate(*exactCandidates.first(), EsiMatchKind::ExactIdentity, slave);
    }
    if (exactCandidates.size() > 1) {
        EsiMatchResult result;
        result.reason = QStringLiteral("多个 ESI 设备具有相同的厂商 ID（Vendor ID）、产品代码（Product Code）和修订号（Revision）");
        return result;
    }

    const QString normalizedOnlineName = normalizedDeviceName(slave.name);
    if (normalizedOnlineName.isEmpty()) {
        EsiMatchResult result;
        result.reason = QStringLiteral("未找到身份完全匹配的 ESI，且在线从站名称为空");
        return result;
    }

    QVector<const EsiDevice*> nameCandidates;
    for (const EsiDevice& device : devices_) {
        if (normalizedDeviceName(device.name) == normalizedOnlineName
            || normalizedDeviceName(device.typeName) == normalizedOnlineName) {
            nameCandidates.push_back(&device);
        }
    }

    if (nameCandidates.size() == 1) {
        return resultForCandidate(*nameCandidates.first(), EsiMatchKind::UniqueNormalizedName, slave);
    }

    EsiMatchResult result;
    result.reason = nameCandidates.isEmpty()
        ? QStringLiteral("未找到与从站身份或规范化名称匹配的 ESI 设备")
        : QStringLiteral("规范化后的从站名称匹配了多个 ESI 设备");
    return result;
}

void EsiRepository::replaceDevices(QVector<EsiDevice> devices)
{
    directory_.clear();
    diagnostics_.clear();
    devices_ = std::move(devices);
}

EsiMatchResult EsiRepository::resultForCandidate(const EsiDevice& device,
                                                 EsiMatchKind kind,
                                                 const OnlineSlaveIdentity& slave) const
{
    EsiMatchResult result;
    result.matched = true;
    result.kind = kind;
    result.device = device;
    result.mappingChecked = slave.activeMappingKnown;

    if (slave.activeMappingKnown) {
        const QVector<ActivePdoEntry> expected = flattenedPdoSignature(device);
        result.mappingCompatible = pdoSignaturesEqual(expected, slave.activePdoEntries);
        if (!result.mappingCompatible) {
            result.reason = signatureMismatchReason(expected, slave.activePdoEntries);
            return result;
        }
    }

    if (kind == EsiMatchKind::UniqueNormalizedName && !slave.activeMappingKnown) {
        result.reason = QStringLiteral("按名称匹配需要先确认当前活动 PDO 映射");
        return result;
    }

    result.mappingCompatible = !slave.activeMappingKnown || result.mappingCompatible;
    result.trusted = true;
    result.reason = kind == EsiMatchKind::ExactIdentity
        ? QStringLiteral("已按完整设备身份匹配 ESI")
        : QStringLiteral("已按唯一的规范化名称和当前活动 PDO 映射匹配 ESI");
    return result;
}

} // namespace explorer

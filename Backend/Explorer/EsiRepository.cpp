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
        return QStringLiteral("Active PDO entry count is %1, ESI declares %2")
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
        return QStringLiteral("Active PDO entry %1 does not match ESI (%2 %3:%4/%5 bits)")
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
                                QStringLiteral("ESI directory does not exist")});
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
        result.reason = QStringLiteral("Multiple ESI devices have the same Vendor ID, Product Code and Revision");
        return result;
    }

    const QString normalizedOnlineName = normalizedDeviceName(slave.name);
    if (normalizedOnlineName.isEmpty()) {
        EsiMatchResult result;
        result.reason = QStringLiteral("No exact ESI identity match and the online slave name is empty");
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
        ? QStringLiteral("No ESI device matches the slave identity or normalized name")
        : QStringLiteral("The normalized slave name matches multiple ESI devices");
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
        result.reason = QStringLiteral("Name fallback requires a known active PDO mapping");
        return result;
    }

    result.mappingCompatible = !slave.activeMappingKnown || result.mappingCompatible;
    result.trusted = true;
    result.reason = kind == EsiMatchKind::ExactIdentity
        ? QStringLiteral("Matched exact ESI identity")
        : QStringLiteral("Matched unique normalized name and active PDO mapping");
    return result;
}

} // namespace explorer

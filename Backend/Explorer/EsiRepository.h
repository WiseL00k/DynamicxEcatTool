#pragma once

#include "Backend/Explorer/EsiParser.h"

namespace explorer {

struct EsiRepositoryIndexResult
{
    QString directory;
    int filesScanned{0};
    int devicesIndexed{0};
    QVector<ParseDiagnostic> diagnostics;

    bool ok() const;
};

class EsiRepository
{
public:
    EsiRepository() = default;
    explicit EsiRepository(EsiParser parser);

    EsiRepositoryIndexResult indexDirectory(const QString& directoryPath);
    void clear();

    const QVector<EsiDevice>& devices() const;
    const QVector<ParseDiagnostic>& diagnostics() const;
    QString directory() const;

    EsiMatchResult match(const OnlineSlaveIdentity& slave) const;

    // Useful for unit tests and callers that already maintain their own file index.
    void replaceDevices(QVector<EsiDevice> devices);

private:
    EsiMatchResult resultForCandidate(const EsiDevice& device,
                                      EsiMatchKind kind,
                                      const OnlineSlaveIdentity& slave) const;

    EsiParser parser_;
    QString directory_;
    QVector<EsiDevice> devices_;
    QVector<ParseDiagnostic> diagnostics_;
};

} // namespace explorer

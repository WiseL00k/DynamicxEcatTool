#pragma once

#include "Backend/Explorer/ExplorerTypes.h"

#include <QIODevice>

namespace explorer {

struct EsiParseResult
{
    QVector<EsiDevice> devices;
    QVector<ParseDiagnostic> diagnostics;

    bool ok() const;
};

class EsiParser
{
public:
    EsiParseResult parseFile(const QString& filePath) const;
    EsiParseResult parse(QIODevice* device, const QString& sourceName = {}) const;

    static bool parseUnsigned(const QString& text, quint64* value);
};

} // namespace explorer

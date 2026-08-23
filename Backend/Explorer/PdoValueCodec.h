#pragma once

#include "Backend/Explorer/ExplorerTypes.h"

namespace explorer {

struct PdoDecodeResult
{
    bool ok{false};
    QVariant value;
    QString displayValue;
    QString error;
};

struct PdoEncodeResult
{
    bool ok{false};
    QString error;
};

class PdoValueCodec
{
public:
    static PdoDecodeResult decode(const QByteArray& processImage,
                                  qsizetype bitOffset,
                                  int bitLength,
                                  const QString& dataType);

    // The target is changed only after the input and range have been validated.
    static PdoEncodeResult encode(const QVariant& value,
                                  const QString& dataType,
                                  int bitLength,
                                  QByteArray* processImage,
                                  qsizetype bitOffset);

    static bool isBooleanType(const QString& dataType);
    static bool isSignedIntegerType(const QString& dataType);
    static bool isUnsignedIntegerType(const QString& dataType);
    static bool isFloatingPointType(const QString& dataType);
    static bool isStringType(const QString& dataType);
    static bool isRawType(const QString& dataType);
};

} // namespace explorer

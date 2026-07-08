#pragma once

#include "SOEM_interface/EcatMasterBus.h"
#include <QString>
#include <QVariantList>
#include <vector>

namespace Backend {

struct SdoConfigMappingResult
{
    bool ok{false};
    std::vector<soem_interface::SDOConfig> configs;
    QString errorMessage;
};

SdoConfigMappingResult mapSdoConfigs(const QVariantList& list);
std::vector<soem_interface::SDOConfig> toSdoConfigs(const QVariantList& list);

} // namespace Backend

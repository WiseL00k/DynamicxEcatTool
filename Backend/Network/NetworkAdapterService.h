#pragma once

#include "SOEM_interface/SoemUtils.h"

#include <QString>
#include <QStringList>
#include <string>
#include <vector>

namespace Backend {

class NetworkAdapterService
{
public:
    using AdapterInfo = soem_interface::SoemUtils::AdapterInfo;

    static std::vector<AdapterInfo> scan();

    void replaceAdapters(std::vector<AdapterInfo> adapters);
    QStringList descriptions() const;
    bool selectAdapter(int index, std::string& nicName, QString& errorMessage) const;

private:
    std::vector<AdapterInfo> adapters_;
};

} // namespace Backend

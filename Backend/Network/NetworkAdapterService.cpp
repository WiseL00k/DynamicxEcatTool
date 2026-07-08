#include "NetworkAdapterService.h"

#include <utility>

namespace Backend {

std::vector<NetworkAdapterService::AdapterInfo> NetworkAdapterService::scan()
{
    return soem_interface::SoemUtils::scanAdapters();
}

void NetworkAdapterService::replaceAdapters(std::vector<AdapterInfo> adapters)
{
    adapters_ = std::move(adapters);
}

QStringList NetworkAdapterService::descriptions() const
{
    QStringList result;
    for (const auto& adapter : adapters_) {
        result << QString::fromStdString(adapter.desc);
    }
    return result;
}

bool NetworkAdapterService::selectAdapter(int index, std::string& nicName, QString& errorMessage) const
{
    if (index < 0 || index >= static_cast<int>(adapters_.size())) {
        errorMessage = QStringLiteral("无效网卡选择，请刷新网卡列表后重试");
        return false;
    }

    nicName = adapters_[static_cast<size_t>(index)].name;
    return true;
}

} // namespace Backend


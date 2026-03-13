#include "SOEM_interface/SoemUtils.h"

extern "C" {
#include <soem/soem.h>
}
namespace soem_interface{
    std::vector<SoemUtils::AdapterInfo> SoemUtils::scanAdapters()
    {
        std::vector<AdapterInfo> adapters;

        ec_adaptert* adapter = ec_find_adapters();
        ec_adaptert* current = adapter;

        while (current != nullptr)
        {
            adapters.push_back({
                current->name,
                current->desc
            });
            current = current->next;
        }

        ec_free_adapters(adapter);
        return adapters;
    }
}

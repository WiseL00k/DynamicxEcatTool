#include "SOEM_interface/EcatSlaveBase.h"

namespace soem_interface{
EcatSlaveBase::EcatSlaveBase(EcatMasterBus* bus, const uint32_t address)
    : bus_(bus), address_(address)
{}
}

#pragma once

#include "Backend/Config/DxSlaveConfigurationParser.h"
#include "Backend/Config/EcatSlaveConfigurationParser.h"
#include "SOEM_interface/EcatSlaveBase.h"

#include <QString>
#include <memory>

namespace soem_interface {
class EcatMasterBus;
}

namespace Backend {

std::shared_ptr<soem_interface::EcatSlaveBase> createEcatSlave(
    const EcatSlaveConfiguration& entry,
    const DxSlave::DxSlaveConfiguration& configuration,
    soem_interface::EcatMasterBus& master,
    QString& errorMessage);

} // namespace Backend

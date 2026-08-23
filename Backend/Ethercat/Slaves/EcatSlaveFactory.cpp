#include "EcatSlaveFactory.h"

#include "Backend/Ethercat/SlaveTypes.h"
#include "Backend/Ethercat/Slaves/MitEcatSlave.h"
#include "Backend/Ethercat/Slaves/RmEcatSlave.h"
#include "SOEM_interface/EcatMasterBus.h"

namespace Backend {

std::shared_ptr<soem_interface::EcatSlaveBase> createEcatSlave(
    const EcatSlaveConfiguration& entry,
    const DxSlave::DxSlaveConfiguration& configuration,
    soem_interface::EcatMasterBus& master,
    QString& errorMessage)
{
    const auto type = parseSlaveType(entry.type);
    if (!type.has_value()) {
        errorMessage = QStringLiteral("不支持的从站类型: %1").arg(QString::fromStdString(entry.type));
        return nullptr;
    }

    switch (*type) {
    case SlaveType::Rm: {
        auto slave = std::make_shared<rm_ecat_slave::standard::RmEcatSlave>(
            entry.name,
            &master,
            static_cast<uint32_t>(entry.address));
        slave->setConfiguration(configuration);
        return slave;
    }
    case SlaveType::Mit: {
        auto slave = std::make_shared<rm_ecat_slave::mit::MitEcatSlave>(
            entry.name,
            &master,
            static_cast<uint32_t>(entry.address));
        slave->setConfiguration(configuration);
        return slave;
    }
    }

    return nullptr;
}

} // namespace Backend

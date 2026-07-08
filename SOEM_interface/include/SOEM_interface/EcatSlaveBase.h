#ifndef ECATSLAVEBASE_H
#define ECATSLAVEBASE_H

#include "SOEM_interface/soem_interface_export.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace soem_interface{

class EcatMasterBus;

class SOEM_INTERFACE_EXPORT EcatSlaveBase
{
public:
    struct DeviceOnlineStatus {
        std::string name;
        bool online{false};
    };

    /**
   * @brief      Struct defining the Pdo characteristic
   */
    struct PdoInfo {
        // The id of the rx pdo
        uint16_t rxPdoId_ = 0;
        // The id of the tx pdo
        uint16_t txPdoId_ = 0;
        // The size of the rx pdo
        uint16_t rxPdoSize_ = 0;
        // The size of the tx pdo
        uint16_t txPdoSize_ = 0;
        // The value referencing the current pdo type on slave side
        uint32_t moduleId_ = 0;
    };

    EcatSlaveBase(EcatMasterBus* bus, const uint32_t address);
    EcatSlaveBase();
    virtual ~EcatSlaveBase() = default;

    /**
   * @brief      Returns the name of the slave.
   *
   * @return     Name of the ethercat bus
   */
    virtual std::string getName() const = 0;

    virtual std::string getType() const = 0;

    /**
   * @brief      Gets the current pdo information.
   *
   * @return     The current pdo information.
   */
    virtual PdoInfo getCurrentPdoInfo() const = 0;

    /**
   * @brief      Startup non-ethercat specific objects for the slave
   *
   * @return     True if succesful
   */
    virtual bool startup() = 0;

    /**
   * @brief      Called during reading the ethercat bus. Use this method to
   *             extract readings from the ethercat bus buffer
   */
    virtual void updateRead() = 0;

    /**
   * @brief      Called during writing to the ethercat bus. Use this method to
   *             stage a command for the slave
   */
    virtual void updateWrite() = 0;

    /**
   * @brief      Used to shutdown slave specific objects
   */
    virtual void shutdown() = 0;

    virtual uint32_t statuswordRaw() const { return 0; }
    virtual std::vector<DeviceOnlineStatus> collectDeviceOnlineStatuses() const { return {}; }

    void setEthercatBusBasePointer(EcatMasterBus* bus) {bus_ = bus;}
    /**
   * @brief      Returns the bus address of the slave
   *
   * @return     Bus address.
   */
    uint32_t getAddress() const { return address_; }
protected:
    mutable std::recursive_mutex mutex_;
    EcatMasterBus* bus_{nullptr};
    uint32_t address_{0};
};
}
#endif // ECATSLAVEBASE_H

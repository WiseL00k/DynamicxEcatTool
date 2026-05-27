#include "RmEcatSlave.h"

namespace rm_ecat_slave{
namespace standard {

bool Statusword::isImuOnline(CanBus bus) const
{
    static bool imus_online_flag[2]{};
    static uint32_t imus_timeout[2]{};

    if(!isAngularVelocityUpdated(bus) || !isLinearAccelerationUpdated(bus))
    {
        imus_timeout[static_cast<int>(bus)]++;
    }
    else
    {
        imus_online_flag[static_cast<int>(bus)] = true;
        imus_timeout[static_cast<int>(bus)] = 0;
    }
    if(imus_timeout[static_cast<int>(bus)] > 20)
    {
        imus_timeout[static_cast<int>(bus)] = 20;
        imus_online_flag[static_cast<int>(bus)] = false;
    }
    return imus_online_flag[static_cast<int>(bus)];
}

bool Statusword::isAngularVelocityUpdated(CanBus bus) const {
    return (statusword_ & (1 << (4 * static_cast<uint8_t>(bus) + 16))) > 0;
}

bool Statusword::isLinearAccelerationUpdated(CanBus bus) const {
    return (statusword_ & (1 << (4 * static_cast<uint8_t>(bus) + 16 + 1))) > 0;
}

bool RmEcatSlave::startup()
{
    pdoInfo_.rxPdoSize_ = sizeof(RxPdo);
    pdoInfo_.txPdoSize_ = sizeof(TxPdo);
    return true;
}

/**
   * @brief      Called during reading the ethercat bus. Use this method to
   *             extract readings from the ethercat bus buffer
   */
void RmEcatSlave::updateRead()
{
    std::lock_guard<std::mutex> lock(readingMutex_);
    bus_->readTxPdo(address_, sizeof(txPdo), &txPdo);
    reading_.setStatusword(txPdo.statusword_);
}

/**
   * @brief      Called during writing to the ethercat bus. Use this method to
   *             stage a command for the slave
   */
void RmEcatSlave::updateWrite()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    bus_->writeRxPdo(address_, sizeof(rxPdo), &rxPdo);
}

/**
   * @brief      Used to shutdown slave specific objects
   */
void RmEcatSlave::shutdown()
{

}

}

namespace mit {
bool MitEcatSlave::startup()
{
    pdoInfo_.rxPdoSize_ = sizeof(RxPdo);
    pdoInfo_.txPdoSize_ = sizeof(TxPdo);
    return true;
}

/**
   * @brief      Called during reading the ethercat bus. Use this method to
   *             extract readings from the ethercat bus buffer
   */
void MitEcatSlave::updateRead()
{
    std::lock_guard<std::mutex> lock(readingMutex_);
    bus_->readTxPdo(address_, sizeof(txPdo), &txPdo);
    reading_.setStatusword(txPdo.statusword_);
}

/**
   * @brief      Called during writing to the ethercat bus. Use this method to
   *             stage a command for the slave
   */
void MitEcatSlave::updateWrite()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    command_.toRxPdo(rxPdo);
    bus_->writeRxPdo(address_, sizeof(rxPdo), &rxPdo);
}

/**
   * @brief      Used to shutdown slave specific objects
   */
void MitEcatSlave::shutdown()
{

}

void MitEcatSlave::clearCommand()
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    command_.clear();
}
}
}

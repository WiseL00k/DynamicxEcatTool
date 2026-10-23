#pragma once

#include "SOEM_interface/DcTestTypes.h"
extern "C" {
#include "soem/soem.h"
}

namespace soem_interface::detail {

// The caller owns the mailbox/configuration and context locks. These helpers
// never change PDO mappings, start a worker, or take ownership of the socket.
bool configureSync0(ecx_contextt& context, DcTestSnapshot& snapshot, std::string& error);
bool disableSync0(ecx_contextt& context, uint16_t slave);

} // namespace soem_interface::detail

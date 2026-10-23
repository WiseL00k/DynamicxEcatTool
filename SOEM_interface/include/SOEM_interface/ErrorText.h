#pragma once

#include <cstdint>
#include <string>

#include "SOEM_interface/soem_interface_export.h"

namespace soem_interface {

// UTF-8 descriptions of the codes in this repository's SOEM/src/ec_print.c.
// Unknown codes retain their original hexadecimal value in the description.
SOEM_INTERFACE_EXPORT std::string alStatusText(uint16_t code);
SOEM_INTERFACE_EXPORT std::string sdoAbortText(uint32_t code);
SOEM_INTERFACE_EXPORT std::string soeErrorText(uint16_t code);
SOEM_INTERFACE_EXPORT std::string mailboxErrorText(uint16_t code);

} // namespace soem_interface

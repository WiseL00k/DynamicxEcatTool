#pragma once

#include "SOEM_interface/SoemUtils.h"
#include <QString>

namespace Backend {

QString toUserMessage(soem_interface::error::SoemInterfaceErrorCode code);

} // namespace Backend

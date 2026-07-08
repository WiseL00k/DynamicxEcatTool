#pragma once

#include <optional>
#include <string>

namespace Backend {

enum class SlaveType
{
    Rm,
    Mit
};

std::optional<SlaveType> parseSlaveType(const std::string& value);
const char* slaveTypeName(SlaveType type);

} // namespace Backend

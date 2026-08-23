#include "SlaveTypes.h"

#include <algorithm>

namespace Backend {
namespace {

std::string normalized(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

std::optional<SlaveType> parseSlaveType(const std::string& value)
{
    const std::string type = normalized(value);
    if (type == "rm") {
        return SlaveType::Rm;
    }
    if (type == "mit") {
        return SlaveType::Mit;
    }
    return std::nullopt;
}

const char* slaveTypeName(SlaveType type)
{
    switch (type) {
    case SlaveType::Rm:
        return "Rm";
    case SlaveType::Mit:
        return "Mit";
    }
    return "Unknown";
}

} // namespace Backend
#include <cctype>



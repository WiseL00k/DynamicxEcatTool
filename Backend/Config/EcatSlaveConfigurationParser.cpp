#include "EcatSlaveConfigurationParser.h"

#include "Backend/Ethercat/SlaveTypes.h"

#include <filesystem>
#include <stdexcept>

namespace Backend {
namespace {

std::string requiredString(const YAML::Node& node, const char* key)
{
    if (!node[key]) {
        throw std::runtime_error(std::string("[EcatSlaveConfigurationParser] Device entry has no ") + key);
    }
    return node[key].as<std::string>();
}

int requiredInt(const YAML::Node& node, const char* key)
{
    if (!node[key]) {
        throw std::runtime_error(std::string("[EcatSlaveConfigurationParser] Device entry has no ") + key);
    }
    return node[key].as<int>();
}

} // namespace

EcatSlaveConfigurationParser::EcatSlaveConfigurationParser(const std::string& filename)
    : YamlConfigurationParserBase(filename)
{
    parse();
}

EcatSlaveConfigurationParser::EcatSlaveConfigurationParser(const YAML::Node& node)
    : YamlConfigurationParserBase(node)
{
    parse();
}

void EcatSlaveConfigurationParser::parseConfiguration(const YAML::Node& configNode)
{
    if (!configNode["ethercat_master"]) {
        throw std::runtime_error("[EcatSlaveConfigurationParser] Node ethercat_master is missing in yaml");
    }

    const auto masterNode = configNode["ethercat_master"];
    if (!masterNode["time_step"]) {
        throw std::runtime_error("[EcatSlaveConfigurationParser] Node time_step missing in ethercat_master");
    }

    if (!configNode["ethercat_devices"]) {
        throw std::runtime_error("[EcatSlaveConfigurationParser] Node ethercat_devices missing in yaml");
    }

    const YAML::Node devices = configNode["ethercat_devices"];
    if (!devices.IsSequence() || devices.size() == 0) {
        throw std::runtime_error("[EcatSlaveConfigurationParser] No devices defined in yaml");
    }

    const std::filesystem::path configPath(configFilePath_);
    const auto configDir = configPath.parent_path();

    for (const YAML::Node& child : devices) {
        EcatSlaveConfiguration entry;
        entry.type = requiredString(child, "type");
        if (!parseSlaveType(entry.type).has_value()) {
            throw std::runtime_error("[EcatSlaveConfigurationParser] Unsupported slave type: " + entry.type);
        }

        entry.name = requiredString(child, "name");
        entry.configuration_file_path = requiredString(child, "configuration_file");
        entry.address = requiredInt(child, "ethercat_address");

        if (entry.address <= 0 || entry.address > 255) {
            throw std::runtime_error("[EcatSlaveConfigurationParser] ethercat_address out of range: " + std::to_string(entry.address));
        }

        entry.configuration_file_path = (configDir / entry.configuration_file_path).lexically_normal().string();

        const auto [_, inserted] = configuration_.slaveConfigurations_.insert({static_cast<uint8_t>(entry.address), entry});
        if (!inserted) {
            throw std::runtime_error("[EcatSlaveConfigurationParser] Duplicated ethercat_address: " + std::to_string(entry.address));
        }
    }
}

} // namespace Backend

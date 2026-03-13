#ifndef ECATSLAVECONFIGURATIONPARSER_H
#define ECATSLAVECONFIGURATIONPARSER_H

#include "ConfigurationParserBase.h"
#include <unordered_map>

namespace Backend{
class EcatSlaveConfiguration
{
public:
    std::string type;
    std::string name;
    std::string configuration_file_path;
    int address;
};

class EcatDeviceConfiguration
{
public:
    std::unordered_map<uint8_t, EcatSlaveConfiguration> slaveConfigurations_;
};

class EcatSlaveConfigurationParser : public Backend::YamlConfigurationParserBase<EcatDeviceConfiguration>
{
public:
    explicit EcatSlaveConfigurationParser(const std::string& filename)
        : YamlConfigurationParserBase(filename)
    {
        parse();   // 子类构造完成后再解析
    }

    explicit EcatSlaveConfigurationParser(const YAML::Node& node)
        : YamlConfigurationParserBase(node)
    {
        parse();
    }

    ~EcatSlaveConfigurationParser() override = default;
protected:
    void parseConfiguration(const YAML::Node& configNode) override
    {
        if (configNode["ethercat_master"]) {
            const auto masterNode = configNode["ethercat_master"];
            if (masterNode["time_step"]) {
            } else {
                throw std::runtime_error("[EcatSlaveConfigurationParser] Node time_step missing in ethercat_master");
            }
        } else {
            throw std::runtime_error("[EcatSlaveConfigurationParser] Node ethercat_master is missing in yaml");
        }

        if (configNode["ethercat_devices"]) {
            // Get all children
            const YAML::Node& nodes = configNode["ethercat_devices"];
            if (nodes.size() == 0) {
                throw std::runtime_error("[EcatSlaveConfigurationParser] No devices defined in yaml");
            }

            // Iterate through child nodes
            for (YAML::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
                const YAML::Node& child = *it;
                EcatSlaveConfiguration entry;
                // type
                if (child["type"]) {
                    auto type_str = child["type"].as<std::string>();
                    entry.type = type_str;
                }
                // name
                if (child["name"]) {
                    entry.name = child["name"].as<std::string>();
                } else {
                    throw std::runtime_error("[EcatSlaveConfigurationParser] Node: " + child.Tag() + " has no entry name");
                }

                // configuration_file
                if (child["configuration_file"]) {
                    entry.configuration_file_path = child["configuration_file"].as<std::string>();
                } else {
                    throw std::runtime_error("[EcatSlaveConfigurationParser] Node: " + child.Tag() + " has no entry configuration_file");
                }

                // ethercat_bus_address
                if (child["ethercat_address"]) {
                    entry.address = child["ethercat_address"].as<int>();
                } else {
                    throw std::runtime_error("[EcatSlaveConfigurationParser] Node: " + child.Tag() + " has no entry ethercat_bus_address");
                }

                std::filesystem::path p(configFilePath_);
                auto dir = p.parent_path();

                entry.configuration_file_path =
                    (dir / entry.configuration_file_path).string();
                configuration_.slaveConfigurations_.insert({entry.address, entry});
            }
        } else {
            throw std::runtime_error("[EcatSlaveConfigurationParser] Node ethercat_devices missing in yaml");
        }
    }

};

}

#endif // ECATSLAVECONFIGURATIONPARSER_H

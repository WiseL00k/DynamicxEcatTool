#ifndef ECATSLAVECONFIGURATIONPARSER_H
#define ECATSLAVECONFIGURATIONPARSER_H

#include "Backend/Config/ConfigurationParserBase.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Backend {

class EcatSlaveConfiguration
{
public:
    std::string type;
    std::string name;
    std::string configuration_file_path;
    int address{0};
};

class EcatDeviceConfiguration
{
public:
    std::unordered_map<uint8_t, EcatSlaveConfiguration> slaveConfigurations_;
};

class EcatSlaveConfigurationParser : public YamlConfigurationParserBase<EcatDeviceConfiguration>
{
public:
    explicit EcatSlaveConfigurationParser(const std::string& filename);
    explicit EcatSlaveConfigurationParser(const YAML::Node& node);
    ~EcatSlaveConfigurationParser() override = default;

protected:
    void parseConfiguration(const YAML::Node& configNode) override;
};

} // namespace Backend

#endif // ECATSLAVECONFIGURATIONPARSER_H


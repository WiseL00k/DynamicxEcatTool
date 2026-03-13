#pragma once

#include <filesystem>
#include <yaml-cpp/yaml.h>
#include <iostream>

namespace Backend {
template<typename Configuration>
class YamlConfigurationParserBase
{
public:
    YamlConfigurationParserBase() = delete;

    explicit YamlConfigurationParserBase(const std::string& filename)
    {
        if (!pathExists(filename)) {
            throw std::runtime_error("[YamlConfigurationParserBase] File not found: " + filename);
        }
        configFilePath_ = filename;
        rootNode_ = YAML::LoadFile(filename);
    }

    explicit YamlConfigurationParserBase(const YAML::Node& configNode)
    {
        rootNode_ = configNode;
    }

    virtual ~YamlConfigurationParserBase() {}

    Configuration getConfiguration() const { return configuration_; }
protected:
    void parse()
    {
        parseConfiguration(rootNode_);
    }
    template <typename T>
    bool getValueFromFile(YAML::Node& yamlNode, const std::string& varName, T& var)
    {
        if (!yamlNode[varName].IsDefined()) {
            std::cerr << "[YamlConfigurationParserBase]: field '" << varName << "' is missing. Default value will be used.";
            return false;
        }
        try {
            T tmpVar = yamlNode[varName].as<T>();
            var = tmpVar;
            return true;
        } catch (...) {
            std::cerr << "[YamlConfigurationParserBase] Error while parsing value \"" << varName
                                                                                                              << "\", default values will be used";
            return false;
        }
    }

    virtual void parseConfiguration(const YAML::Node& configNode) = 0;
    YAML::Node rootNode_;
    std::string configFilePath_;
    Configuration configuration_{};

private:
    bool pathExists(const std::string& path)
    {
        return std::filesystem::exists(path);
    }
};
}

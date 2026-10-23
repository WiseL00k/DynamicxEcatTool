#pragma once

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <yaml-cpp/yaml.h>

namespace Backend {

inline std::string formatYamlDiagnostic(const YAML::Exception& error,
                                        const std::string& source)
{
    std::string reason = "YAML 配置解析失败";
    if (dynamic_cast<const YAML::BadFile*>(&error)) {
        reason = "无法打开或读取 YAML 配置文件";
    } else if (dynamic_cast<const YAML::BadConversion*>(&error)
               || dynamic_cast<const YAML::InvalidScalar*>(&error)) {
        reason = "YAML 字段值无法转换为所需类型，请检查数值格式和取值范围";
    } else if (dynamic_cast<const YAML::InvalidNode*>(&error)) {
        reason = "YAML 节点无效，请检查字段是否存在及列表、键值映射的结构";
    } else if (dynamic_cast<const YAML::BadSubscript*>(&error)) {
        reason = "YAML 节点不支持按字段或索引访问，请检查是否误写为单个值";
    } else if (dynamic_cast<const YAML::KeyNotFound*>(&error)) {
        reason = "YAML 配置缺少所需字段";
    } else if (dynamic_cast<const YAML::ParserException*>(&error)) {
        const std::string& message = error.msg;
        if (message == YAML::ErrorMsg::END_OF_MAP || message == YAML::ErrorMsg::END_OF_MAP_FLOW) {
            reason = "YAML 键值映射未正确结束，请检查缩进、冒号和花括号";
        } else if (message == YAML::ErrorMsg::END_OF_SEQ || message == YAML::ErrorMsg::END_OF_SEQ_FLOW) {
            reason = "YAML 列表未正确结束，请检查缩进、分隔符和方括号";
        } else if (message == YAML::ErrorMsg::TAB_IN_INDENTATION) {
            reason = "YAML 缩进包含制表符，请使用空格缩进";
        } else if (message == YAML::ErrorMsg::MAP_KEY || message == YAML::ErrorMsg::MAP_VALUE) {
            reason = "YAML 键或值的位置不合法，请检查缩进和冒号";
        } else if (message == YAML::ErrorMsg::EOF_IN_SCALAR) {
            reason = "YAML 字段值尚未结束文件就已结束，请检查引号和内容是否完整";
        } else if (message == YAML::ErrorMsg::FLOW_END || message == YAML::ErrorMsg::BLOCK_ENTRY
                   || message == YAML::ErrorMsg::UNEXPECTED_FLOW) {
            reason = "YAML 列表或映射的分隔符位置不合法，请检查括号、逗号和缩进";
        } else if (message.find(YAML::ErrorMsg::INVALID_ESCAPE) == 0) {
            reason = "YAML 字符串包含无效转义字符，请检查反斜杠后的内容";
        } else if (message.find(YAML::ErrorMsg::INVALID_UNICODE) == 0) {
            reason = "YAML 字符串包含无效的 Unicode 编码";
        } else if (message == YAML::ErrorMsg::CHAR_IN_SCALAR || message == YAML::ErrorMsg::INVALID_HEX
                   || message == YAML::ErrorMsg::SINGLE_QUOTED_CHAR) {
            reason = "YAML 字段值包含不合法字符，请检查引号、转义和数值格式";
        } else if (message.find(YAML::ErrorMsg::UNKNOWN_ANCHOR) == 0
                   || message == YAML::ErrorMsg::ALIAS_NOT_FOUND || message == YAML::ErrorMsg::ANCHOR_NOT_FOUND) {
            reason = "YAML 锚点或别名未定义或缺少名称";
        } else {
            reason = "YAML 语法错误，请检查标记位置附近的缩进、字段和分隔符";
        }
    }
    if (!source.empty()) reason += "（文件：" + source + "）";
    if (!error.mark.is_null()) {
        reason += "，第 " + std::to_string(error.mark.line + 1) + " 行，第 "
            + std::to_string(error.mark.column + 1) + " 列";
    } else {
        reason += "（解析器未提供行列位置）";
    }
    return reason + "；原始诊断：" + error.what();
}

template<typename Configuration>
class YamlConfigurationParserBase
{
public:
    YamlConfigurationParserBase() = delete;

    explicit YamlConfigurationParserBase(const std::string& filename)
    {
        if (!pathExists(filename)) {
            throw std::runtime_error("[YamlConfigurationParserBase] 配置文件不存在：" + filename);
        }

        configFilePath_ = filename;
        rootNode_ = YAML::LoadFile(filename);
    }

    explicit YamlConfigurationParserBase(const YAML::Node& configNode)
        : rootNode_(configNode)
    {}

    virtual ~YamlConfigurationParserBase() = default;

    Configuration getConfiguration() const { return configuration_; }

protected:
    void parse()
    {
        parseConfiguration(rootNode_);
    }

    template <typename T>
    bool getValueFromFile(const YAML::Node& yamlNode, const std::string& varName, T& var)
    {
        lastValueDiagnostic_.clear();
        if (!yamlNode[varName].IsDefined()) {
            lastValueDiagnostic_ = "缺少字段“" + varName + "”";
            std::cerr << "[YamlConfigurationParserBase] " << lastValueDiagnostic_
                      << "，将使用默认值" << std::endl;
            return false;
        }

        try {
            var = yamlNode[varName].as<T>();
            return true;
        } catch (const YAML::Exception& error) {
            lastValueDiagnostic_ = formatYamlDiagnostic(error, configFilePath_);
        } catch (const std::exception& error) {
            lastValueDiagnostic_ = std::string("解析失败；原始诊断：") + error.what();
        } catch (...) {
            lastValueDiagnostic_ = "解析时发生未知异常";
        }
        std::cerr << "[YamlConfigurationParserBase] 字段“" << varName << "”解析失败，将使用默认值；"
                  << lastValueDiagnostic_ << std::endl;
        return false;
    }

    virtual void parseConfiguration(const YAML::Node& configNode) = 0;

    YAML::Node rootNode_;
    std::string configFilePath_;
    std::string lastValueDiagnostic_;
    Configuration configuration_{};

private:
    static bool pathExists(const std::string& path)
    {
        return std::filesystem::exists(path);
    }
};

} // namespace Backend

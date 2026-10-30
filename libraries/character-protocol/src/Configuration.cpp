#include <CharacterChat/Configuration.h>

#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace characterchat {
namespace {

using Json = nlohmann::json;

Json convert(const toml::node& node, unsigned depth, unsigned& nodes) {
    if (depth > 24 || ++nodes > 16000) {
        throw std::runtime_error("TOML: 配置层级或条目数量超出限制。");
    }
    if (const auto* table = node.as_table()) {
        Json result = Json::object();
        for (const auto& [key, value] : *table) {
            result[std::string(key.str())] = convert(value, depth + 1, nodes);
        }
        return result;
    }
    if (const auto* array = node.as_array()) {
        Json result = Json::array();
        for (const auto& value : *array) {
            result.push_back(convert(value, depth + 1, nodes));
        }
        return result;
    }
    if (node.is_string()) {
        return *node.value<std::string>();
    }
    if (node.is_integer()) {
        const auto value = *node.value<int64_t>();
        return value >= 0 ? Json(uint64_t(value)) : Json(value);
    }
    if (node.is_boolean()) {
        return *node.value<bool>();
    }
    if (node.is_floating_point() && std::isfinite(*node.value<double>())) {
        return *node.value<double>();
    }
    throw std::runtime_error("TOML: 配置不支持日期、时间或非有限数值。");
}

template <typename Container, typename Key>
void append(Container& container, const Key& key, const Json& value);

toml::table makeTable(const Json& value) {
    toml::table table;
    for (const auto& [key, item] : value.items()) {
        append(table, key, item);
    }
    return table;
}

toml::array makeArray(const Json& value) {
    toml::array array;
    for (const auto& item : value) {
        append(array, 0, item);
    }
    return array;
}

template <typename Container, typename Key>
void append(Container& container, const Key& key, const Json& value) {
    const auto insert = [&](auto item) {
        if constexpr (std::is_same_v<Container, toml::table>) {
            container.insert(key, std::move(item));
        } else {
            container.push_back(std::move(item));
        }
    };
    if (value.is_object()) {
        insert(makeTable(value));
    } else if (value.is_array()) {
        insert(makeArray(value));
    } else if (value.is_string()) {
        insert(value.get<std::string>());
    } else if (value.is_boolean()) {
        insert(value.get<bool>());
    } else if (value.is_number_integer()) {
        insert(value.get<int64_t>());
    } else if (value.is_number_float()) {
        insert(value.get<double>());
    } else {
        throw std::runtime_error("TOML: 无法保存此配置字段。");
    }
}

}  // namespace

nlohmann::json parseConfiguration(std::string_view text, std::string_view filename) {
    if (text.empty() || text.size() > 256 * 1024 || text.find('\0') != std::string_view::npos) {
        throw std::runtime_error("TOML: 配置为空或超过 256 KiB。");
    }
    try {
        const auto table = toml::parse(text, filename);
        unsigned nodes = 0;
        return convert(table, 0, nodes);
    } catch (const toml::parse_error& error) {
        throw std::runtime_error("TOML: " + std::string(filename) + " 第 " + std::to_string(error.source().begin.line) +
                                 " 行，第 " + std::to_string(error.source().begin.column) + " 列存在语法错误。");
    }
}

std::string writeConfiguration(const nlohmann::json& document) {
    std::ostringstream output;
    output << "# 人物素材配置（UTF-8 / TOML 1.0）。图片和视频均为同目录下的独立文件。\n"
              "# 修改文件、情绪 emotion、场景 scenes 或 character_template.prompt 后重新导入此目录。\n"
              "# 不需要填写 sha256、bytes、width、height、fps 或 duration_ms，导入时会自动检测。\n"
              "# file 使用相对于此配置所在目录的路径和 /；禁止绝对路径、..、链接文件。\n"
              "# emotion: neutral / happy / sad / shy / angry / surprised / worried / thoughtful。\n"
              "# usages: expression（表情）、pose（动作）、background（背景）、media（可发送）。\n"
              "# pose_wave / pose_folded 分别映射 wave / folded 动作；neutral 表情必须存在。\n"
              "# media 中 tags 用于筛选，required_flags / minimum_turns / maximum_sends 控制发送条件。\n\n";
    output << toml::toml_formatter{makeTable(document)};
    return output.str();
}

}  // namespace characterchat

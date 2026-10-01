#ifndef BTW_LAUNCHER_JSON_H
#define BTW_LAUNCHER_JSON_H

#include <string>
#include <utility>
#include <vector>

// ==================== 极简 JSON 解析 ====================
// 只解析遥测协议用到的子集：对象 / 数组 / 数字 / 字符串 / 布尔 / null。

struct JsonValue
{
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> items;                              // Array
    std::vector<std::pair<std::string, JsonValue>> fields;     // Object

    const JsonValue *Find(const std::string &key) const;
    const JsonValue *At(std::size_t index) const;
    std::size_t Size() const;
    double NumberOr(double fallback = 0.0) const;
    bool BoolOr(bool fallback = false) const;
};

bool JsonParse(const std::string &text, JsonValue &out, std::string &error);

#endif  // BTW_LAUNCHER_JSON_H

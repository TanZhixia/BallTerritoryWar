#include "json.h"

#include <cstdlib>
#include <cstring>

namespace {

struct Parser
{
    const std::string &text;
    std::size_t pos = 0;
    std::string error;

    explicit Parser(const std::string &t) : text(t) {}

    void SkipWhitespace()
    {
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' ||
                text[pos] == '\r')) {
            ++pos;
        }
    }

    bool Fail(const std::string &message)
    {
        if (error.empty()) {
            error = message + "（位置 " + std::to_string(pos) + "）";
        }
        return false;
    }

    bool ParseValue(JsonValue &out)
    {
        SkipWhitespace();
        if (pos >= text.size()) {
            return Fail("意外的结尾");
        }
        const char c = text[pos];
        if (c == '{') {
            return ParseObject(out);
        }
        if (c == '[') {
            return ParseArray(out);
        }
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return ParseString(out.str);
        }
        if (text.compare(pos, 4, "true") == 0) {
            pos += 4;
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (text.compare(pos, 5, "false") == 0) {
            pos += 5;
            out.type = JsonValue::Type::Bool;
            out.boolean = false;
            return true;
        }
        if (text.compare(pos, 4, "null") == 0) {
            pos += 4;
            out.type = JsonValue::Type::Null;
            return true;
        }
        return ParseNumber(out);
    }

    bool ParseNumber(JsonValue &out)
    {
        const char *start = text.c_str() + pos;
        char *end = nullptr;
        const double value = std::strtod(start, &end);
        if (end == start) {
            return Fail("无法解析的值");
        }
        pos += static_cast<std::size_t>(end - start);
        out.type = JsonValue::Type::Number;
        out.number = value;
        return true;
    }

    bool ParseString(std::string &out)
    {
        if (pos >= text.size() || text[pos] != '"') {
            return Fail("需要字符串");
        }
        ++pos;
        while (pos < text.size()) {
            const char c = text[pos];
            if (c == '"') {
                ++pos;
                return true;
            }
            if (c == '\\') {
                ++pos;
                if (pos >= text.size()) {
                    break;
                }
                switch (text[pos]) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        // 仅处理基本多文种平面：转成 UTF-8
                        if (pos + 4 >= text.size()) {
                            return Fail("\\u 转义不完整");
                        }
                        const std::string hex = text.substr(pos + 1, 4);
                        const unsigned int cp =
                            static_cast<unsigned int>(std::strtoul(hex.c_str(), nullptr, 16));
                        pos += 4;
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += text[pos]; break;
                }
                ++pos;
                continue;
            }
            out += c;
            ++pos;
        }
        return Fail("字符串未闭合");
    }

    bool ParseArray(JsonValue &out)
    {
        out.type = JsonValue::Type::Array;
        ++pos;  // '['
        SkipWhitespace();
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            return true;
        }
        while (true) {
            JsonValue item;
            if (!ParseValue(item)) {
                return false;
            }
            out.items.push_back(std::move(item));
            SkipWhitespace();
            if (pos >= text.size()) {
                return Fail("数组未闭合");
            }
            if (text[pos] == ',') {
                ++pos;
                continue;
            }
            if (text[pos] == ']') {
                ++pos;
                return true;
            }
            return Fail("数组中出现意外字符");
        }
    }

    bool ParseObject(JsonValue &out)
    {
        out.type = JsonValue::Type::Object;
        ++pos;  // '{'
        SkipWhitespace();
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        while (true) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (pos >= text.size() || text[pos] != ':') {
                return Fail("对象缺少 ':'");
            }
            ++pos;
            JsonValue value;
            if (!ParseValue(value)) {
                return false;
            }
            out.fields.emplace_back(std::move(key), std::move(value));
            SkipWhitespace();
            if (pos >= text.size()) {
                return Fail("对象未闭合");
            }
            if (text[pos] == ',') {
                ++pos;
                continue;
            }
            if (text[pos] == '}') {
                ++pos;
                return true;
            }
            return Fail("对象中出现意外字符");
        }
    }
};

}  // namespace

const JsonValue *JsonValue::Find(const std::string &key) const
{
    for (const auto &kv : fields) {
        if (kv.first == key) {
            return &kv.second;
        }
    }
    return nullptr;
}

const JsonValue *JsonValue::At(std::size_t index) const
{
    if (index >= items.size()) {
        return nullptr;
    }
    return &items[index];
}

std::size_t JsonValue::Size() const
{
    if (type == Type::Array) {
        return items.size();
    }
    if (type == Type::Object) {
        return fields.size();
    }
    return 0;
}

double JsonValue::NumberOr(double fallback) const
{
    return type == Type::Number ? number : fallback;
}

bool JsonValue::BoolOr(bool fallback) const
{
    return type == Type::Bool ? boolean : fallback;
}

bool JsonParse(const std::string &text, JsonValue &out, std::string &error)
{
    Parser parser(text);
    if (!parser.ParseValue(out)) {
        error = parser.error;
        return false;
    }
    parser.SkipWhitespace();
    if (parser.pos != text.size()) {
        error = "JSON 尾部有多余内容";
        return false;
    }
    return true;
}

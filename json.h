// json.h - Simple JSON parser for C++11
// Header-only implementation
#ifndef JSON_H
#define JSON_H

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>

namespace json {

enum Type {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
};

class Value;
typedef std::vector<Value> Array;
typedef std::map<std::string, Value> Object;

class Value {
public:
    Type type;
    bool bool_val;
    double num_val;
    std::string str_val;
    Array arr_val;
    Object obj_val;

    Value() : type(JSON_NULL), bool_val(false), num_val(0) {}
    Value(std::nullptr_t) : type(JSON_NULL), bool_val(false), num_val(0) {}
    Value(bool b) : type(JSON_BOOL), bool_val(b), num_val(0) {}
    Value(int n) : type(JSON_NUMBER), bool_val(false), num_val(n) {}
    Value(double n) : type(JSON_NUMBER), bool_val(false), num_val(n) {}
    Value(const std::string& s) : type(JSON_STRING), bool_val(false), num_val(0), str_val(s) {}
    Value(const char* s) : type(JSON_STRING), bool_val(false), num_val(0), str_val(s) {}
    Value(const Array& a) : type(JSON_ARRAY), bool_val(false), num_val(0), arr_val(a) {}
    Value(const Object& o) : type(JSON_OBJECT), bool_val(false), num_val(0), obj_val(o) {}

    bool isNull() const { return type == JSON_NULL; }
    bool isBool() const { return type == JSON_BOOL; }
    bool isNumber() const { return type == JSON_NUMBER; }
    bool isString() const { return type == JSON_STRING; }
    bool isArray() const { return type == JSON_ARRAY; }
    bool isObject() const { return type == JSON_OBJECT; }

    bool asBool() const { return bool_val; }
    double asNumber() const { return num_val; }
    int asInt() const { return static_cast<int>(num_val); }
    const std::string& asString() const { return str_val; }
    const Array& asArray() const { return arr_val; }
    const Object& asObject() const { return obj_val; }

    Array& asArray() { return arr_val; }
    Object& asObject() { return obj_val; }

    bool has(const std::string& key) const {
        return type == JSON_OBJECT && obj_val.find(key) != obj_val.end();
    }

    const Value& operator[](const std::string& key) const {
        static Value null_val;
        if (type != JSON_OBJECT) return null_val;
        Object::const_iterator it = obj_val.find(key);
        return it != obj_val.end() ? it->second : null_val;
    }

    Value& operator[](const std::string& key) {
        if (type != JSON_OBJECT) {
            type = JSON_OBJECT;
            obj_val.clear();
        }
        return obj_val[key];
    }

    const Value& operator[](size_t index) const {
        static Value null_val;
        if (type != JSON_ARRAY || index >= arr_val.size()) return null_val;
        return arr_val[index];
    }

    Value& operator[](size_t index) {
        if (type != JSON_ARRAY) {
            type = JSON_ARRAY;
            arr_val.clear();
        }
        if (index >= arr_val.size()) {
            arr_val.resize(index + 1);
        }
        return arr_val[index];
    }

    size_t size() const {
        if (type == JSON_ARRAY) return arr_val.size();
        if (type == JSON_OBJECT) return obj_val.size();
        return 0;
    }
};

// Parser
class Parser {
public:
    Parser(const std::string& input) : input_(input), pos_(0) {}

    Value parse() {
        skipWhitespace();
        Value val = parseValue();
        skipWhitespace();
        return val;
    }

private:
    std::string input_;
    size_t pos_;

    char peek() const {
        if (pos_ >= input_.size()) return '\0';
        return input_[pos_];
    }

    char get() {
        if (pos_ >= input_.size()) throw std::runtime_error("Unexpected end of input");
        return input_[pos_++];
    }

    void skipWhitespace() {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' ||
               input_[pos_] == '\n' || input_[pos_] == '\r')) {
            pos_++;
        }
    }

    Value parseValue() {
        skipWhitespace();
        char c = peek();
        if (c == '"') return parseString();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        throw std::runtime_error("Invalid JSON");
    }

    Value parseString() {
        get(); // consume '"'
        std::string result;
        while (true) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char esc = get();
                switch (esc) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u': {
                        std::string hex;
                        for (int i = 0; i < 4; i++) hex += get();
                        unsigned int cp = std::strtoul(hex.c_str(), nullptr, 16);
                        if (cp < 0x80) {
                            result += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            result += static_cast<char>(0xC0 | (cp >> 6));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (cp >> 12));
                            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: result += esc; break;
                }
            } else {
                result += c;
            }
        }
        return Value(result);
    }

    Value parseNumber() {
        size_t start = pos_;
        if (peek() == '-') get();
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') get();
        if (peek() == '.') {
            get();
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') get();
        }
        if (peek() == 'e' || peek() == 'E') {
            get();
            if (peek() == '+' || peek() == '-') get();
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') get();
        }
        return Value(std::atof(input_.substr(start, pos_ - start).c_str()));
    }

    Value parseBool() {
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return Value(true);
        }
        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return Value(false);
        }
        throw std::runtime_error("Invalid JSON");
    }

    Value parseNull() {
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return Value(nullptr);
        }
        throw std::runtime_error("Invalid JSON");
    }

    Value parseArray() {
        get(); // consume '['
        Array arr;
        skipWhitespace();
        if (peek() == ']') {
            get();
            return Value(arr);
        }
        while (true) {
            arr.push_back(parseValue());
            skipWhitespace();
            if (peek() == ',') {
                get();
                skipWhitespace();
            } else if (peek() == ']') {
                get();
                break;
            } else {
                throw std::runtime_error("Invalid JSON");
            }
        }
        return Value(arr);
    }

    Value parseObject() {
        get(); // consume '{'
        Object obj;
        skipWhitespace();
        if (peek() == '}') {
            get();
            return Value(obj);
        }
        while (true) {
            skipWhitespace();
            Value key = parseString();
            skipWhitespace();
            if (get() != ':') throw std::runtime_error("Invalid JSON");
            obj[key.asString()] = parseValue();
            skipWhitespace();
            if (peek() == ',') {
                get();
                skipWhitespace();
            } else if (peek() == '}') {
                get();
                break;
            } else {
                throw std::runtime_error("Invalid JSON");
            }
        }
        return Value(obj);
    }
};

inline Value parse(const std::string& input) {
    Parser parser(input);
    return parser.parse();
}

// Escape function - preserves UTF-8 characters (Chinese, etc.)
inline std::string escape(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                // Only escape ASCII control characters (< 0x20)
                // UTF-8 characters (>= 0x80) are preserved as-is
                if (c < 0x20) {
                    char buf[8];
                    std::sprintf(buf, "\\u%04x", c);
                    result += buf;
                } else {
                    // Keep UTF-8 bytes as-is (including Chinese characters)
                    result += static_cast<char>(c);
                }
                break;
        }
    }
    return result;
}

inline std::string serialize(const Value& val, bool pretty = false, int indent = 0) {
    std::string result;
    std::string indentStr = pretty ? std::string(indent * 2, ' ') : "";
    std::string nextIndentStr = pretty ? std::string((indent + 1) * 2, ' ') : "";

    switch (val.type) {
        case JSON_NULL: result = "null"; break;
        case JSON_BOOL: result = val.bool_val ? "true" : "false"; break;
        case JSON_NUMBER: {
            char buf[64];
            if (val.num_val == static_cast<int>(val.num_val)) {
                std::sprintf(buf, "%d", static_cast<int>(val.num_val));
            } else {
                std::sprintf(buf, "%.17g", val.num_val);
            }
            result = buf;
            break;
        }
        case JSON_STRING:
            result = "\"" + escape(val.str_val) + "\"";
            break;
        case JSON_ARRAY: {
            result = "[";
            if (pretty) result += "\n" + nextIndentStr;
            for (size_t i = 0; i < val.arr_val.size(); i++) {
                if (i > 0) {
                    result += ",";
                    if (pretty) result += "\n" + nextIndentStr;
                }
                result += serialize(val.arr_val[i], pretty, indent + 1);
            }
            if (pretty) result += "\n" + indentStr;
            result += "]";
            break;
        }
        case JSON_OBJECT: {
            result = "{";
            if (pretty) result += "\n" + nextIndentStr;
            size_t count = 0;
            for (Object::const_iterator it = val.obj_val.begin(); it != val.obj_val.end(); ++it) {
                if (count > 0) {
                    result += ",";
                    if (pretty) result += "\n" + nextIndentStr;
                }
                result += "\"" + escape(it->first) + "\":";
                if (pretty) result += " ";
                result += serialize(it->second, pretty, indent + 1);
                count++;
            }
            if (pretty) result += "\n" + indentStr;
            result += "}";
            break;
        }
    }

    return result;
}

} // namespace json

#endif // JSON_H

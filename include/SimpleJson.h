#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <map>

// Simples e robusto serializador/desserializador JSON sem dependências externas
namespace SimpleJson {
    inline std::string escapeString(const std::string& str) {
        std::ostringstream ss;
        for (char c : str) {
            if (c == '"') ss << "\\\"";
            else if (c == '\\') ss << "\\\\";
            else if (c == '\b') ss << "\\b";
            else if (c == '\f') ss << "\\f";
            else if (c == '\n') ss << "\\n";
            else if (c == '\r') ss << "\\r";
            else if (c == '\t') ss << "\\t";
            else ss << c;
        }
        return ss.str();
    }

    struct Node {
        enum Type { Null, Bool, Number, String, Array, Object };
        Type type = Null;
        bool boolVal = false;
        double numVal = 0.0;
        std::string strVal;
        std::vector<Node> arr;
        std::map<std::string, Node> obj;

        Node() = default;
        Node(bool b) : type(Bool), boolVal(b) {}
        Node(double d) : type(Number), numVal(d) {}
        Node(int i) : type(Number), numVal(i) {}
        Node(const std::string& s) : type(String), strVal(s) {}
        Node(const char* s) : type(String), strVal(s) {}

        bool isObject() const {
            return type == Object;
        }
        bool isArray() const {
            return type == Array;
        }
        bool isNumber() const {
            return type == Number;
        }
        bool isString() const {
            return type == String;
        }

        const Node& operator[](const std::string& key) const {
            static Node empty;
            if (type != Object) return empty;
            auto it = obj.find(key);
            if (it != obj.end()) return it->second;
            return empty;
        }

        const Node& operator[](size_t idx) const {
            static Node empty;
            if (type != Array || idx >= arr.size()) return empty;
            return arr[idx];
        }

        double asDouble(double def = 0.0) const {
            if (type == Number) return numVal;
            if (type == String) {
                try {
                    return std::stod(strVal);
                } catch (...) {}
            }
            return def;
        }

        int asInt(int def = 0) const {
            if (type == Number) return static_cast<int>(numVal);
            if (type == String) {
                try {
                    return std::stoi(strVal);
                } catch (...) {}
            }
            return def;
        }

        std::string asString(const std::string& def = "") const {
            if (type == String) return strVal;
            return def;
        }

        std::string serialize(int indent = 2, int currentIndent = 0) const {
            std::string ind(currentIndent, ' ');
            std::string nextInd(currentIndent + indent, ' ');
            std::ostringstream ss;

            switch (type) {
                case Null:
                    ss << "null";
                    break;
                case Bool:
                    ss << (boolVal ? "true" : "false");
                    break;
                case Number:
                    if (std::floor(numVal) == numVal && std::abs(numVal) < 1e15) {
                        ss << static_cast<long long>(numVal);
                    } else {
                        ss << std::fixed << std::setprecision(4) << numVal;
                    }
                    break;
                case String:
                    ss << "\"" << escapeString(strVal) << "\"";
                    break;
                case Array: {
                    if (arr.empty()) {
                        ss << "[]";
                        break;
                    }
                    ss << "[\n";
                    for (size_t i = 0; i < arr.size(); ++i) {
                        ss << nextInd << arr[i].serialize(indent, currentIndent + indent);
                        if (i + 1 < arr.size()) ss << ",";
                        ss << "\n";
                    }
                    ss << ind << "]";
                    break;
                }
                case Object: {
                    if (obj.empty()) {
                        ss << "{}";
                        break;
                    }
                    ss << "{\n";
                    size_t i = 0;
                    for (const auto& kv : obj) {
                        ss << nextInd << "\"" << escapeString(kv.first) << "\": "
                        << kv.second.serialize(indent, currentIndent + indent);
                        if (++i < obj.size()) ss << ",";
                        ss << "\n";
                    }
                    ss << ind << "}";
                    break;
                }
            }
            return ss.str();
        }
    };

    class Parser {
        std::string src;
        size_t pos = 0;

        void skipWhitespace() {
            while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n')) {
                pos++;
            }
        }

        std::string parseString() {
            pos++; // skip "
            std::string res;
            while (pos < src.size()) {
                char c = src[pos++];
                if (c == '"') return res;
                if (c == '\\' && pos < src.size()) {
                    char esc = src[pos++];
                    if (esc == 'n') res += '\n';
                    else if (esc == 't') res += '\t';
                    else if (esc == 'r') res += '\r';
                    else if (esc == '"') res += '"';
                    else if (esc == '\\') res += '\\';
                    else res += esc;
                } else {
                    res += c;
                }
            }
            return res;
        }

        Node parseNumber() {
            size_t start = pos;
            if (src[pos] == '-') pos++;
            while (pos < src.size() && (isdigit(src[pos]) || src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' || src[pos] == '+' || src[pos] == '-')) {
                pos++;
            }
            double val = std::stod(src.substr(start, pos - start));
            return Node(val);
        }

    public:
        Parser(const std::string& input) : src(input), pos(0) {}

        Node parseValue() {
            skipWhitespace();
            if (pos >= src.size()) return Node();

            char c = src[pos];
            if (c == '{') {
                pos++;
                Node node;
                node.type = Node::Object;
                skipWhitespace();
                if (pos < src.size() && src[pos] == '}') {
                    pos++;
                    return node;
                }
                while (pos < src.size()) {
                    skipWhitespace();
                    if (src[pos] != '"') break;
                    std::string key = parseString();
                    skipWhitespace();
                    if (pos < src.size() && src[pos] == ':') pos++;
                    Node val = parseValue();
                    node.obj[key] = val;
                    skipWhitespace();
                    if (pos < src.size() && src[pos] == ',') {
                        pos++;
                        continue;
                    }
                    if (pos < src.size() && src[pos] == '}') {
                        pos++;
                        break;
                    }
                }
                return node;
            } else if (c == '[') {
                pos++;
                Node node;
                node.type = Node::Array;
                skipWhitespace();
                if (pos < src.size() && src[pos] == ']') {
                    pos++;
                    return node;
                }
                while (pos < src.size()) {
                    Node val = parseValue();
                    node.arr.push_back(val);
                    skipWhitespace();
                    if (pos < src.size() && src[pos] == ',') {
                        pos++;
                        continue;
                    }
                    if (pos < src.size() && src[pos] == ']') {
                        pos++;
                        break;
                    }
                }
                return node;
            } else if (c == '"') {
                return Node(parseString());
            } else if (c == 't' || c == 'f') {
                if (src.compare(pos, 4, "true") == 0) {
                    pos += 4;
                    return Node(true);
                }
                if (src.compare(pos, 5, "false") == 0) {
                    pos += 5;
                    return Node(false);
                }
            } else if (c == 'n') {
                if (src.compare(pos, 4, "null") == 0) {
                    pos += 4;
                    return Node();
                }
            } else if (c == '-' || isdigit(c)) {
                return parseNumber();
            }
            return Node();
        }
    };

    inline Node parse(const std::string& jsonStr) {
        Parser p(jsonStr);
        return p.parseValue();
    }

    inline Node parseFile(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.is_open()) return Node();
        std::stringstream buffer;
        buffer << f.rdbuf();
        return parse(buffer.str());
    }
}

#pragma once

#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>

class IniConfig {
public:
    std::map<std::string, std::map<std::string, std::string>> data;

    static std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    bool load(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        std::string line;
        std::string current_section = "general";

        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line.front() == '[' && line.back() == ']') {
                current_section = trim(line.substr(1, line.size() - 2));
                std::transform(current_section.begin(), current_section.end(), current_section.begin(), ::tolower);
            } else {
                size_t eq = line.find('=');
                if (eq != std::string::npos) {
                    std::string key = trim(line.substr(0, eq));
                    std::string val = trim(line.substr(eq + 1));
                    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                    data[current_section][key] = val;
                }
            }
        }
        return true;
    }

    std::string getString(const std::string& section, const std::string& key, const std::string& defVal = "") const {
        std::string sec = section;
        std::string k = key;
        std::transform(sec.begin(), sec.end(), sec.begin(), ::tolower);
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);

        auto itSec = data.find(sec);
        if (itSec != data.end()) {
            auto itKey = itSec->second.find(k);
            if (itKey != itSec->second.end()) {
                return itKey->second;
            }
        }
        return defVal;
    }

    double getDouble(const std::string& section, const std::string& key, double defVal = 0.0) const {
        std::string s = getString(section, key, "");
        if (s.empty()) return defVal;
        try {
            return std::stod(s);
        } catch (...) {
            return defVal;
        }
    }

    int getInt(const std::string& section, const std::string& key, int defVal = 0) const {
        std::string s = getString(section, key, "");
        if (s.empty()) return defVal;
        try {
            return std::stoi(s);
        } catch (...) {
            return defVal;
        }
    }

    bool getBool(const std::string& section, const std::string& key, bool defVal = false) const {
        std::string s = getString(section, key, "");
        if (s.empty()) return defVal;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return (s == "1" || s == "true" || s == "yes" || s == "on" || s == "sim");
    }
};

#include "configmgr.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// 去除字符串首尾的空白
static void trim(std::string& s) {
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    auto e = s.find_last_not_of(ws);
    if (b == std::string::npos) {
        s.clear();
    }
    else {
        s = s.substr(b, e - b + 1);
    }
}

bool ConfigMgr::loadFromFile(const std::string &filepath)
{
    std::ifstream in(filepath);
    if (!in.is_open())   return false;
    section_.clear();
    // 重置状态
    kv_.clear();

    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find_first_of(";#");
        if (pos != std::string::npos) {
            line.resize(pos);
        }
        trim(line);
        if (line.empty())   continue;

        // 处理section
        if (line.front() == '[' && line.back() == ']') {
            section_ = line.substr(1, line.size() - 2);
            trim(section_);
            continue;
        }

        // 处理key = value
        auto eq = line.find('=');
        if (eq == std::string::npos)    continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key);
        trim(value);
        if (!section_.empty()) {
            key = section_ + "." + key;
        }
        kv_[key] = value;
    }
    return true;
}

/**********************************
 * Full specialization of fromSring
 **********************************/
template<>
int ConfigMgr::fromString<int>(const std::string& s) {
    return std::stoi(s);
}

template<>
long ConfigMgr::fromString<long>(const std::string& s) {
    return std::stol(s);
}

template<>
double ConfigMgr::fromString<double>(const std::string& s) {
    return std::stod(s);
}

template<>
bool ConfigMgr::fromString<bool>(const std::string& s) {
    // 创建字符串副本
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {return std::tolower(c);});
    return (t == "1" || t == "true" || t == "yes");
}

template<>
std::string ConfigMgr::fromString<std::string>(const std::string& s) {
    return s;
}

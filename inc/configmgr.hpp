#ifndef __CONFIG_MGR_H__
#define __CONFIG_MGR_H__

#include <string>
#include <unordered_map>

class ConfigMgr {
public:
    bool loadFromFile(const std::string& filepath);

    template<typename T>
    T get(const std::string& key, const T& defaultValue) const {
        auto it = kv_.find(key);
        if (it == kv_.end())    return defaultValue;
        return fromString<T>(it->second);
    }

    bool hasKey(const std::string& key) const {
        return kv_.count(key) > 0;
    }

    static ConfigMgr& Inst() {
        static ConfigMgr mgr;
        return mgr;
    }
private:
    ConfigMgr() = default;
    ConfigMgr(const ConfigMgr&) = delete;
    ConfigMgr& operator=(const ConfigMgr&) = delete;
    std::string section_;
    std::unordered_map<std::string, std::string> kv_;

    template<typename T>
    static T fromString(const std::string& s);
};

#endif

template <typename T>
inline T ConfigMgr::fromString(const std::string &s)
{
    return T();
}

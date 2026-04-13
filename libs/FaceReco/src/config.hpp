#pragma once
#include <string>
#include <unordered_map>

class Settings {
public:
    // Load settings from key=value file. Returns true on success.
    static bool load(const std::string &path);

    // getters with defaults
    static int getInt(const std::string &key, int def);
    static float getFloat(const std::string &key, float def);
    static bool getBool(const std::string &key, bool def);
    static std::string getString(const std::string &key, const std::string &def);

private:
    static std::unordered_map<std::string, std::string> values_;
    static void trim(std::string &s);
};

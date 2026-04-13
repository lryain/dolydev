#include "config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

std::unordered_map<std::string, std::string> Settings::values_;

void Settings::trim(std::string &s) {
    while(!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while(!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
}

bool Settings::load(const std::string &path) {
    values_.clear();
    std::ifstream ifs(path);
    if(!ifs.is_open()) return false;
    std::string line;
    while(std::getline(ifs, line)) {
        // strip comments
        auto comment = line.find('#');
        if(comment!=std::string::npos) line = line.substr(0, comment);
        auto eq = line.find('=');
        if(eq==std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq+1);
        trim(key); trim(val);
        if(key.empty()) continue;
        values_[key] = val;
    }
    return true;
}

int Settings::getInt(const std::string &key, int def) {
    auto it = values_.find(key);
    if(it==values_.end()) return def;
    try { return std::stoi(it->second); } catch(...) { return def; }
}

float Settings::getFloat(const std::string &key, float def) {
    auto it = values_.find(key);
    if(it==values_.end()) return def;
    try { return std::stof(it->second); } catch(...) { return def; }
}

bool Settings::getBool(const std::string &key, bool def) {
    auto it = values_.find(key);
    if(it==values_.end()) return def;
    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    if(v=="1" || v=="true" || v=="yes") return true;
    if(v=="0" || v=="false" || v=="no") return false;
    return def;
}

std::string Settings::getString(const std::string &key, const std::string &def) {
    auto it = values_.find(key);
    if(it==values_.end()) return def;
    return it->second;
}

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace doly::eye_engine::json_utils {

std::optional<std::string> extractObject(const std::string& src,
                                         std::string_view key,
                                         char open = '{',
                                         char close = '}');

std::optional<std::string> extractArray(const std::string& src,
                                        std::string_view key);

std::optional<std::string> extractString(const std::string& src,
                                         std::string_view key);

double extractDouble(const std::string& src,
                     std::string_view key,
                     double fallback);

int extractInt(const std::string& src,
               std::string_view key,
               int fallback);

bool extractBool(const std::string& src,
                 std::string_view key,
                 bool fallback);

std::vector<int> parseIntArray(const std::string& content);

std::vector<double> parseNumberArray(const std::string& content);

std::map<std::string, std::string> parseStringMap(const std::string& content);

std::map<std::string, std::string> extractObjectSections(const std::string& content);

std::vector<std::string> parseStringArray(const std::string& content);

std::vector<std::string> parseObjectArray(const std::string& content);

}  // namespace doly::eye_engine::json_utils

#pragma once

#include "autocharge/types.hpp"

#include <string>

namespace doly::autocharge {

bool loadServiceConfig(const std::string& path, ServiceConfig& config, std::string* error_message = nullptr);

}  // namespace doly::autocharge
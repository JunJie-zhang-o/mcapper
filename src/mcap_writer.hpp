#pragma once

#include <string>
#include <vector>

#include <mcapper/logger_options.hpp>
#include <mcapper/record.hpp>

namespace mcapper::detail {

bool writeMcap(const std::string& path,
               const LoggerOptions& options,
               const std::vector<Record>& records);

}  // namespace mcapper::detail

#pragma once

#include "cli/parser.hpp"

#include <optional>

namespace pp {

int runPluginCommandCli(const Args& args);
std::optional<int> tryDispatchPlugin(const Args& args);

}  // namespace pp

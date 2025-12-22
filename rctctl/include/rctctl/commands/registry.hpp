#pragma once

#include <string>
#include <vector>

#include "rctctl/cli/cli.hpp"

namespace rctctl::commands {

const std::vector<cli::CommandSpec>& GetCommandRegistry();
const cli::CommandSpec* FindCommandSpec(const std::string& resource, const std::vector<std::string>& path);
void PrintUsage();
bool PrintResourceUsage(const std::string& resource);
bool PrintSubcommandUsage(const std::string& resource, const std::vector<std::string>& pathPrefix);
void PrintCommandHelp(const cli::CommandSpec& spec);

} // namespace rctctl::commands

#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace rctctl::cli {

struct ParsedArgs
{
    std::unordered_map<std::string, std::string> values;
};

ParsedArgs ParseCommandArguments(const std::vector<std::string>& tokens);
std::optional<std::string> GetStringOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys);
int ParseIntValue(const std::string& value, const std::string& name);
std::optional<int> GetIntOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys);
int RequireIntOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys, std::string description);
std::string RequireStringOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys, std::string description);
double ParseDoubleValue(const std::string& value, const std::string& name);
std::optional<double> GetDoubleOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys);
double RequireDoubleOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys, std::string description);
bool ParseBoolValue(const std::string& value, const std::string& name);
std::optional<bool> GetBoolOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys);
uint64_t ParseUint64Value(const std::string& value, const std::string& name);
std::optional<uint64_t> GetUint64Option(const ParsedArgs& args, std::initializer_list<std::string_view> keys);
void ApplyRideSelector(nlohmann::json& params, const ParsedArgs& args);
nlohmann::json SplitCommaSeparated(const std::string& value);

struct CommandPlan
{
    std::string method;
    nlohmann::json params;
};

struct CommandArgSpec
{
    std::string flag;
    std::string description;
    bool required = false;
    std::string valueName;
};

struct CommandSpec
{
    std::string resource;
    std::vector<std::string> path;
    std::string summary;
    std::string help;
    std::vector<CommandArgSpec> args;
    std::function<CommandPlan(const ParsedArgs&)> buildPlan;
    std::function<void(const nlohmann::json&)> textRenderer;
};

struct ParsedCli
{
    std::string resource;
    std::string action;
    std::vector<std::string> commandTokens;
    bool jsonOutput = false;
    bool helpRequested = false;
    bool resourceHelpRequested = false;
    std::string host = "127.0.0.1";
    uint16_t port = 9876;
    std::vector<std::string> columns;
    std::optional<std::string> filter;
    bool watch = false;
    std::optional<int> watchIntervalSeconds;
};

ParsedCli ParseCli(int argc, char** argv);

struct CommandTokenSplit
{
    std::vector<std::string> subcommands;
    std::vector<std::string> flagTokens;
    bool helpRequested = false;
};

CommandTokenSplit SplitCommandTokens(const std::vector<std::string>& tokens);

std::vector<std::string> NormalisePath(const std::vector<std::string>& tokens);
std::string BuildCommandLabel(const std::string& resource, const std::vector<std::string>& path);

} // namespace rctctl::cli

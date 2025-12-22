#include "rctctl/cli/cli.hpp"

#include "rctctl/util/string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace rctctl::cli {
namespace {
constexpr const char* kOutputJson = "json";
constexpr const char* kOutputText = "text";

std::string TrimCopy(std::string value)
{
    auto begin = value.find_first_not_of(" \t\n\r");
    auto end = value.find_last_not_of(" \t\n\r");
    if (begin == std::string::npos)
    {
        return std::string();
    }
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> ParseColumnList(const std::string& value)
{
    std::vector<std::string> columns;
    std::string token;
    std::stringstream ss(value);
    while (std::getline(ss, token, ','))
    {
        auto trimmed = TrimCopy(token);
        if (!trimmed.empty())
        {
            columns.push_back(trimmed);
        }
    }
    return columns;
}
}

using json = nlohmann::json;

ParsedArgs ParseCommandArguments(const std::vector<std::string>& tokens)
{
    ParsedArgs parsed;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const auto& token = tokens[i];
        if (token.rfind("--", 0) != 0)
        {
            std::ostringstream msg;
            msg << "Invalid: Unexpected positional argument: " << token
                << ". rctctl commands only accept --flag value pairs (e.g. --type " << token << ")";
            throw std::runtime_error(msg.str());
        }

        std::string key;
        std::string value;
        auto eqPos = token.find('=');
        if (eqPos != std::string::npos)
        {
            key = token.substr(2, eqPos - 2);
            value = token.substr(eqPos + 1);
        }
        else
        {
            key = token.substr(2);
            // Check if next token exists and is not a flag (doesn't start with --)
            // If next token is another flag or we're at the end, treat as presence-only boolean flag
            if (i + 1 >= tokens.size() || tokens[i + 1].rfind("--", 0) == 0)
            {
                // Presence-only boolean flag (e.g., --small means --small true)
                value = "true";
            }
            else
            {
                value = tokens[++i];
            }
        }
        parsed.values[util::ToLower(key)] = value;
    }
    return parsed;
}

std::optional<std::string> GetStringOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys)
{
    for (auto key : keys)
    {
        auto it = args.values.find(util::ToLower(std::string(key)));
        if (it != args.values.end())
        {
            return it->second;
        }
    }
    return std::nullopt;
}

int ParseIntValue(const std::string& value, const std::string& name)
{
    try
    {
        size_t processed = 0;
        const int result = std::stoi(value, &processed);
        if (processed != value.size())
        {
            throw std::runtime_error("");
        }
        return result;
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("Invalid: Flag --" + name + " expects an integer (e.g., --id 5)");
    }
}

std::optional<int> GetIntOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys)
{
    if (auto stringValue = GetStringOption(args, keys))
    {
        return ParseIntValue(*stringValue, util::ToLower(std::string(*keys.begin())));
    }
    return std::nullopt;
}

int RequireIntOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys, std::string description)
{
    if (auto value = GetIntOption(args, keys))
    {
        return value.value();
    }
    std::string firstKey(keys.begin()->data(), keys.begin()->size());
    throw std::runtime_error("Invalid: " + description + " is required (use --" + firstKey + " <number>)");
}

std::string RequireStringOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys, std::string description)
{
    if (auto value = GetStringOption(args, keys))
    {
        return *value;
    }
    std::string firstKey(keys.begin()->data(), keys.begin()->size());
    throw std::runtime_error("Invalid: " + description + " is required (use --" + firstKey + " <value>)");
}

double ParseDoubleValue(const std::string& value, const std::string& name)
{
    try
    {
        size_t processed = 0;
        double result = std::stod(value, &processed);
        if (processed != value.size())
        {
            throw std::runtime_error("");
        }
        return result;
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("Invalid: Flag --" + name + " expects a number (e.g., --value 10.5)");
    }
}

std::optional<double> GetDoubleOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys)
{
    if (auto value = GetStringOption(args, keys))
    {
        return ParseDoubleValue(*value, util::ToLower(std::string(*keys.begin())));
    }
    return std::nullopt;
}

double RequireDoubleOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys, std::string description)
{
    if (auto value = GetDoubleOption(args, keys))
    {
        return value.value();
    }
    std::string firstKey(keys.begin()->data(), keys.begin()->size());
    throw std::runtime_error("Invalid: " + description + " is required (use --" + firstKey + " <number>)");
}

bool ParseBoolValue(const std::string& value, const std::string& name)
{
    auto lowered = util::ToLower(value);
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on")
    {
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off")
    {
        return false;
    }
    throw std::runtime_error("Invalid: Flag --" + name + " expects true/false (use true, false, yes, no, 1, or 0)");
}

std::optional<bool> GetBoolOption(const ParsedArgs& args, std::initializer_list<std::string_view> keys)
{
    if (auto stringValue = GetStringOption(args, keys))
    {
        return ParseBoolValue(*stringValue, std::string(*keys.begin()));
    }
    return std::nullopt;
}

uint64_t ParseUint64Value(const std::string& value, const std::string& name)
{
    try
    {
        size_t processed = 0;
        uint64_t result = std::stoull(value, &processed, 0);
        if (processed != value.size())
        {
            throw std::runtime_error("");
        }
        return result;
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("Invalid: Flag --" + name + " expects an integer (use decimal like 123 or hex like 0x7B)");
    }
}

std::optional<uint64_t> GetUint64Option(const ParsedArgs& args, std::initializer_list<std::string_view> keys)
{
    if (auto stringValue = GetStringOption(args, keys))
    {
        return ParseUint64Value(*stringValue, util::ToLower(std::string(*keys.begin())));
    }
    return std::nullopt;
}

void ApplyRideSelector(json& params, const ParsedArgs& args)
{
    if (auto id = GetIntOption(args, { "id", "ride-id" }))
    {
        params["rideId"] = *id;
        return;
    }
    if (auto name = GetStringOption(args, { "name", "ride-name", "ride" }))
    {
        params["rideName"] = *name;
        return;
    }
    throw std::runtime_error("Invalid: Ride selection required (use --id <number> or --name <string>)");
}

nlohmann::json SplitCommaSeparated(const std::string& value)
{
    nlohmann::json result = nlohmann::json::array();
    std::string token;
    std::stringstream ss(value);
    while (std::getline(ss, token, ','))
    {
        token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
        token.erase(std::find_if(token.rbegin(), token.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    })
                        .base(),
            token.end());
        if (!token.empty())
        {
            result.push_back(token);
        }
    }
    return result;
}

ParsedCli ParseCli(int argc, char** argv)
{
    if (argc < 2)
    {
        throw std::runtime_error("Invalid: Expected a resource (use 'rctctl --help' to see available resources)");
    }

    ParsedCli cli;
    size_t index = 1;

    auto consumeGlobalFlag = [&](size_t& idx, bool allowHelp) -> bool {
        const std::string arg = argv[idx];

        auto requireValue = [&](const char* flagName) -> std::string {
            if (idx + 1 >= static_cast<size_t>(argc))
            {
                throw std::runtime_error("Invalid: " + std::string(flagName) + " requires a value");
            }
            return argv[++idx];
        };

        if (arg == "--host")
        {
            cli.host = requireValue("--host");
            return true;
        }
        if (arg == "--port")
        {
            cli.port = static_cast<uint16_t>(ParseIntValue(requireValue("--port"), "port"));
            return true;
        }
        if (arg == "-o" || arg == "--output")
        {
            auto format = util::ToLower(requireValue(arg.c_str()));
            if (format == kOutputJson)
            {
                cli.jsonOutput = true;
            }
            else if (format == kOutputText)
            {
                cli.jsonOutput = false;
            }
            else
            {
                throw std::runtime_error("Invalid: Unknown output format: " + format + " (use --output json or --output text)");
            }
            return true;
        }
        if (arg == "--json")
        {
            cli.jsonOutput = true;
            return true;
        }
        if (arg == "--text")
        {
            cli.jsonOutput = false;
            return true;
        }
        if (arg == "--columns")
        {
            auto parsedColumns = ParseColumnList(requireValue("--columns"));
            cli.columns.insert(cli.columns.end(), parsedColumns.begin(), parsedColumns.end());
            return true;
        }
        if (arg == "--filter")
        {
            cli.filter = requireValue("--filter");
            return true;
        }
        if (arg.rfind("--watch", 0) == 0)
        {
            cli.watch = true;
            auto eqPos = arg.find('=');
            if (eqPos != std::string::npos)
            {
                auto value = arg.substr(eqPos + 1);
                cli.watchIntervalSeconds = ParseIntValue(value, "watch");
            }
            else if (idx + 1 < static_cast<size_t>(argc))
            {
                const std::string nextArg = argv[idx + 1];
                if (nextArg.rfind("--", 0) != 0)
                {
                    cli.watchIntervalSeconds = ParseIntValue(nextArg, "watch");
                    ++idx;
                }
            }
            return true;
        }
        if (allowHelp && arg == "--help")
        {
            cli.helpRequested = true;
            return true;
        }
        return false;
    };

    while (index < static_cast<size_t>(argc))
    {
        if (consumeGlobalFlag(index, true))
        {
            ++index;
            continue;
        }

        cli.resource = argv[index++];
        break;
    }

    if (cli.helpRequested && cli.resource.empty())
    {
        return cli;
    }

    if (cli.resource.empty())
    {
        throw std::runtime_error("Invalid: Expected a resource (use 'rctctl --help' to see available resources)");
    }

    while (index < static_cast<size_t>(argc))
    {
        if (consumeGlobalFlag(index, false))
        {
            ++index;
            continue;
        }

        const std::string token = argv[index];
        if (token == "--help")
        {
            cli.helpRequested = true;
            cli.resourceHelpRequested = true;
            ++index;
            break;
        }

        cli.action = token;
        ++index;
        break;
    }

    if (cli.resourceHelpRequested)
    {
        if (index < static_cast<size_t>(argc))
        {
            throw std::runtime_error("Invalid: --help for a resource must be the final argument");
        }
        return cli;
    }

    if (cli.action.empty())
    {
        throw std::runtime_error("Invalid: Expected an action (use 'rctctl " + cli.resource + " --help' to see available actions)");
    }

    while (index < static_cast<size_t>(argc))
    {
        if (consumeGlobalFlag(index, false))
        {
            ++index;
            continue;
        }

        cli.commandTokens.emplace_back(argv[index++]);
    }

    return cli;
}

CommandTokenSplit SplitCommandTokens(const std::vector<std::string>& tokens)
{
    CommandTokenSplit split;
    size_t index = 0;
    while (index < tokens.size())
    {
        const auto& token = tokens[index];
        if (!token.empty() && token.rfind("--", 0) == 0)
        {
            break;
        }
        split.subcommands.push_back(token);
        ++index;
    }

    for (; index < tokens.size(); ++index)
    {
        split.flagTokens.push_back(tokens[index]);
    }

    std::vector<std::string> filtered;
    filtered.reserve(split.flagTokens.size());
    for (const auto& token : split.flagTokens)
    {
        if (token == "--help")
        {
            split.helpRequested = true;
            continue;
        }
        filtered.push_back(token);
    }
    split.flagTokens = std::move(filtered);
    return split;
}

std::vector<std::string> NormalisePath(const std::vector<std::string>& tokens)
{
    std::vector<std::string> result;
    result.reserve(tokens.size());
    for (const auto& token : tokens)
    {
        result.push_back(util::ToLower(token));
    }
    return result;
}

std::string BuildCommandLabel(const std::string& resource, const std::vector<std::string>& path)
{
    std::ostringstream oss;
    oss << resource;
    for (const auto& part : path)
    {
        oss << ' ' << part;
    }
    return oss.str();
}

} // namespace rctctl::cli

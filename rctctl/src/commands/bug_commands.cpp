#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;

std::string GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&nowTime), "%Y%m%d_%H%M%S");
    return oss.str();
}

void RenderBugReport(const json& result)
{
    if (result.contains("success") && result["success"].get<bool>())
    {
        std::cout << "Bug report saved to: " << result["path"].get<std::string>() << '\n';
    }
    else
    {
        std::cerr << "Failed to save bug report\n";
    }
}
}

void AppendBugCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "bug",
        { "report" },
        "Report a bug or observation.",
        "Creates a timestamped bug report file in the bug_reports/ directory with the provided description. "
        "Message can be provided via --message flag or piped through stdin.",
        { CommandArgSpec{ "message", "Bug description or observation (optional if using stdin)", false, "TEXT" } },
        [](const ParsedArgs& args) {
            std::string message;

            // Try to get message from flag first
            auto messageOpt = cli::GetStringOption(args, { "message", "msg", "description" });
            if (messageOpt)
            {
                message = *messageOpt;
            }
            else
            {
                // Read from stdin if no --message flag provided
                std::string line;
                std::ostringstream buffer;
                while (std::getline(std::cin, line))
                {
                    if (!buffer.str().empty())
                    {
                        buffer << '\n';
                    }
                    buffer << line;
                }
                message = buffer.str();

                if (message.empty())
                {
                    throw std::runtime_error("Invalid: Bug description is required (use --message <text> or provide via stdin)");
                }
            }

            // Build the file path - use OPENRCT2_REPO_ROOT if set, otherwise fall back to AGENT_WORKSPACE or home
            const char* repoRoot = std::getenv("OPENRCT2_REPO_ROOT");
            const char* agentWorkspace = std::getenv("AGENT_WORKSPACE");
            const char* homeDir = std::getenv("HOME");
            if (!repoRoot && !agentWorkspace && !homeDir)
            {
                throw std::runtime_error("Failed to determine bug reports directory");
            }

            std::string bugReportsDir;
            if (repoRoot && *repoRoot)
            {
                bugReportsDir = std::string(repoRoot) + "/bug_reports";
            }
            else if (agentWorkspace && *agentWorkspace)
            {
                bugReportsDir = std::string(agentWorkspace) + "/bug_reports";
            }
            else
            {
                bugReportsDir = std::string(homeDir) + "/.openrct2-agent/bug_reports";
            }

            // Ensure the directory exists
            std::filesystem::create_directories(bugReportsDir);

            std::string timestamp = GetTimestamp();
            std::string filename = bugReportsDir + "/bug_" + timestamp + ".txt";

            // Write the bug report
            std::ofstream file(filename);
            if (!file)
            {
                throw std::runtime_error("Failed to create bug report file: " + filename);
            }

            file << "Bug Report\n";
            file << "==========\n";
            file << "Timestamp: " << timestamp << "\n\n";
            file << "Description:\n";
            file << message << "\n";
            file.close();

            // Return as a local command (special marker method name)
            json result;
            result["success"] = true;
            result["path"] = filename;
            result["timestamp"] = timestamp;

            return CommandPlan{ "__LOCAL__", result };
        },
        RenderBugReport });
}

} // namespace rctctl::commands

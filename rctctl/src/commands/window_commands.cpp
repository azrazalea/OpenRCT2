#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/windows.hpp"

#include <stdexcept>
#include <nlohmann/json.hpp>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;
}

void AppendWindowCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "windows",
        { "list" },
        "List open UI windows.",
        "Enumerates currently open in-game windows in z-order.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "windows.list", json::object() };
        },
        renderers::RenderWindowList });

    specs.push_back(CommandSpec{
        "windows",
        { "close" },
        "Close UI windows.",
        "Closes windows via --id=<handle> or --class=<name> [--number=N]. Handles accept decimal or 0x hex.",
        { CommandArgSpec{ "id", "Window handle from windows.list (decimal or 0x...)", false, "HANDLE" },
          CommandArgSpec{ "class", "Window class (e.g., rideConstruction).", false, "CLASS" },
          CommandArgSpec{ "class-id", "Window class numeric id.", false, "INT" },
          CommandArgSpec{ "number", "Optional window number filter.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto id = cli::GetUint64Option(args, { "id", "handle" }))
            {
                params["id"] = *id;
            }
            if (auto cls = cli::GetStringOption(args, { "class" }))
            {
                params["class"] = *cls;
            }
            if (auto classId = cli::GetIntOption(args, { "class-id" }))
            {
                params["classId"] = *classId;
            }
            if (auto number = cli::GetIntOption(args, { "number" }))
            {
                params["number"] = *number;
            }
            if (params.empty())
            {
                throw std::runtime_error("Invalid: Provide --id or --class/--class-id to identify the window");
            }
            return CommandPlan{ "windows.close", params };
        },
        renderers::RenderWindowClose });
}

} // namespace rctctl::commands

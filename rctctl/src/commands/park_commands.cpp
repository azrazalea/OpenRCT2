#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/park.hpp"

#include <nlohmann/json.hpp>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;
}

void AppendParkCommands(std::vector<CommandSpec>& specs)
{
    auto makeStatusPlan = [](const ParsedArgs&) {
        return CommandPlan{ "park.status", json::object() };
    };

    specs.push_back(CommandSpec{
        "park",
        { "status" },
        "Display scenario and park overview.",
        "Shows scenario name, objective summary, finances, guests, park rating, current in-game date, and recent news.",
        {},
        makeStatusPlan,
        renderers::RenderParkStatus });

    specs.push_back(CommandSpec{
        "park",
        { "info" },
        "Alias for park status.",
        "Prints the same data as `park status` including recent news.",
        {},
        makeStatusPlan,
        renderers::RenderParkStatus });

    specs.push_back(CommandSpec{
        "park",
        { "guests" },
        "Summarize guest counts and park rating.",
        "Returns current guests inside the park, guests heading for the entrance, and the live park rating.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "park.guests", json::object() };
        },
        renderers::RenderParkGuests });

    specs.push_back(CommandSpec{
        "park",
        { "price" },
        "Show current entrance fee.",
        "Fetches the park's admission price and indicates whether the park is currently free to enter.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "park.price", json::object() };
        },
        [](const json& result) { renderers::RenderParkPrice(result, false); } });

    specs.push_back(CommandSpec{
        "park",
        { "price", "set" },
        "Update the entrance fee.",
        "Sets the park admission price (in dollars). Values above the in-game maximum are clamped automatically.",
        { CommandArgSpec{ "value", "Entrance fee to charge (e.g., 25.50).", true, "AMOUNT" } },
        [](const ParsedArgs& args) {
            double amount = cli::RequireDoubleOption(args, { "value", "price" }, "entrance fee");
            json params = json::object();
            params["price"] = amount;
            return CommandPlan{ "park.setEntranceFee", params };
        },
        [](const json& result) { renderers::RenderParkPrice(result, true); } });

    specs.push_back(CommandSpec{
        "park",
        { "open" },
        "Open the park gate.",
        "Sets the park to OPEN state via park.setOpen RPC.",
        {},
        [](const ParsedArgs&) {
            json params = json::object();
            params["open"] = true;
            return CommandPlan{ "park.setOpen", params };
        },
        renderers::RenderParkGateState });

    specs.push_back(CommandSpec{
        "park",
        { "close" },
        "Close the park to new guests.",
        "Sets the park to CLOSED state via park.setOpen RPC.",
        {},
        [](const ParsedArgs&) {
            json params = json::object();
            params["open"] = false;
            return CommandPlan{ "park.setOpen", params };
        },
        renderers::RenderParkGateState });

    specs.push_back(CommandSpec{
        "park",
        { "rating", "history" },
        "Show park-rating history (months).",
        "Returns the monthly park-rating timeline (up to the last 32 months).",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "park.ratingHistory", json::object() };
        },
        renderers::RenderParkRatingHistory });

    specs.push_back(CommandSpec{
        "park",
        { "warnings" },
        "Summarize park health warnings.",
        "Shows hunger/thirst/vandalism warnings and queue hotspots.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "park.warnings", json::object() };
        },
        renderers::RenderParkWarnings });

    // Sandbox commands disabled - don't expose cheat toggles to Claude
    // specs.push_back(CommandSpec{
    //     "park",
    //     { "sandbox", "status" },
    //     "Show sandbox/cheat toggles.",
    //     "Displays sandbox flags (no money, unlock limits, marketing cheats, etc.).",
    //     {},
    //     [](const ParsedArgs&) {
    //         return CommandPlan{ "park.sandboxStatus", json::object() };
    //     },
    //     renderers::RenderSandboxStatus });

    // specs.push_back(CommandSpec{
    //     "park",
    //     { "sandbox", "set" },
    //     "Toggle sandbox/cheat flag.",
    //     "Sets a sandbox toggle via park.sandboxSet --key <flag> --value <true|false>.",
    //     { CommandArgSpec{ "key", "Sandbox flag name (e.g., sandboxMode, ignorePrice).", true, "STRING" },
    //       CommandArgSpec{ "value", "true/false value.", true, "BOOL" } },
    //     [](const ParsedArgs& args) {
    //         json params = json::object();
    //         params["key"] = cli::RequireStringOption(args, { "key" }, "sandbox key");
    //         params["value"] = cli::ParseBoolValue(cli::RequireStringOption(args, { "value" }, "value"), "value");
    //         return CommandPlan{ "park.sandboxSet", params };
    //     },
    //     renderers::RenderSandboxStatus });
}

} // namespace rctctl::commands

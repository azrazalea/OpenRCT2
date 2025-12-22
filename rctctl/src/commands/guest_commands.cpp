#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/guests.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;
}

void AppendGuestCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "guests",
        { "list" },
        "List guests.",
        "Shows up to --limit guest summaries (id, name, state). Results are ID-sorted and can be paged by passing --after <last-id>. Output format is fixed text (json not supported). For detailed guest info including needs and thoughts, use 'guests get --id <id>'.",
        { CommandArgSpec{ "limit", "Max guests to return (default 50).", false, "INT" },
          CommandArgSpec{ "after", "Resume after this guest id (cursor).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            if (auto after = cli::GetIntOption(args, { "after" }))
            {
                params["after"] = *after;
            }
            return CommandPlan{ "guests.list", params };
        },
        renderers::RenderGuestList });

    specs.push_back(CommandSpec{
        "guests",
        { "get" },
        "Inspect a guest.",
        "Shows detailed stats for a guest. Specify via --id or --name (exact match, case-insensitive).",
        { CommandArgSpec{ "id", "Guest identifier.", false, "INT" },
          CommandArgSpec{ "name", "Guest name (exact match, case-insensitive).", false, "NAME" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto id = cli::GetIntOption(args, { "id" }))
            {
                params["id"] = *id;
            }
            else if (auto name = cli::GetStringOption(args, { "name" }))
            {
                params["name"] = *name;
            }
            else
            {
                throw std::runtime_error("Invalid: Provide --id or --name to identify guest");
            }
            return CommandPlan{ "guests.get", params };
        },
        [](const json& result) { renderers::RenderGuestDetail(result); } });

    specs.push_back(CommandSpec{
        "guests",
        { "search" },
        "Search for guests.",
        "Combines --name substring matching with a tile brush (--x/--y plus optional --radius). Results are ID-sorted, "
        "limited to 50 by default, and can be paged by passing --after <last-id>.",
        { CommandArgSpec{ "name", "Case-insensitive substring to match.", false, "TEXT" },
          CommandArgSpec{ "x", "Center tile X coordinate.", false, "INT" },
          CommandArgSpec{ "y", "Center tile Y coordinate.", false, "INT" },
          CommandArgSpec{ "radius", "Tile radius around --x/--y (default 0).", false, "INT" },
          CommandArgSpec{ "limit", "Max guests to return (default 50).", false, "INT" },
          CommandArgSpec{ "after", "Resume after this guest id (cursor).", false, "INT" },
          CommandArgSpec{ "include-outside", "Include guests who already left the park.", false, "BOOL" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto name = cli::GetStringOption(args, { "name" }))
            {
                params["name"] = *name;
            }

            auto x = cli::GetIntOption(args, { "x" });
            auto y = cli::GetIntOption(args, { "y" });
            if (x || y)
            {
                if (!x || !y)
                {
                    throw std::runtime_error("Invalid: --x and --y must be provided together");
                }
                params["x"] = *x;
                params["y"] = *y;
            }
            if (auto radius = cli::GetIntOption(args, { "radius" }))
            {
                if (!x || !y)
                {
                    throw std::runtime_error("Invalid: --radius requires both --x and --y");
                }
                params["radius"] = *radius;
            }
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            if (auto after = cli::GetIntOption(args, { "after" }))
            {
                params["after"] = *after;
            }
            if (auto includeOutside = cli::GetBoolOption(args, { "include-outside", "includeOutside" }))
            {
                params["includeOutside"] = *includeOutside;
            }
            return CommandPlan{ "guests.search", params };
        },
        renderers::RenderGuestList });

    specs.push_back(CommandSpec{
        "guests",
        { "thoughts" },
        "Show most common guest thoughts.",
        "Mirrors the Guests window summary: clusters popular thoughts with sample guest ids. "
        "Results are sorted by count descending (most common first) and paginated; use --offset to fetch subsequent pages.",
        { CommandArgSpec{ "limit", "Thought groups to show (default 20).", false, "INT" },
          CommandArgSpec{ "offset", "Skip first N groups (for pagination).", false, "INT" },
          CommandArgSpec{ "guest-limit", "Sample guests per row (default 3).", false, "INT" },
          CommandArgSpec{ "order", "Sort field: count (default), text, or ride.", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort direction: desc (default) or asc.", false, "DIR" },
          CommandArgSpec{ "ride-only", "true to only include ride-related thoughts.", false, "BOOL" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            // Apply default limit of 20 to prevent overwhelming output
            int limit = 20;
            if (auto limitArg = cli::GetIntOption(args, { "limit" }))
            {
                limit = *limitArg;
            }
            params["limit"] = limit;
            if (auto offset = cli::GetIntOption(args, { "offset" }))
            {
                params["offset"] = *offset;
            }
            // Default guest-limit to 3 for more compact output
            int guestLimit = 3;
            if (auto guestLimitArg = cli::GetIntOption(args, { "guest-limit", "guestLimit" }))
            {
                guestLimit = *guestLimitArg;
            }
            params["guestLimit"] = guestLimit;
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                params["order"] = *order;
            }
            if (auto direction = cli::GetStringOption(args, { "direction" }))
            {
                params["direction"] = *direction;
            }
            if (auto rideOnly = cli::GetBoolOption(args, { "ride-only", "rideOnly" }))
            {
                params["rideOnly"] = *rideOnly;
            }
            return CommandPlan{ "guests.thoughts", params };
        },
        renderers::RenderGuestThoughtSummary });

    specs.push_back(CommandSpec{
        "guests",
        { "moods" },
        "Bucket guests by mood.",
        "Groups guests by happiness band (ecstatic → furious) with representative ids.",
        { CommandArgSpec{ "limit", "Mood rows to show (default all).", false, "INT" },
          CommandArgSpec{ "guest-limit", "Sample guests per mood (default 5).", false, "INT" },
          CommandArgSpec{ "order", "Sort by count, avg, or label.", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort order asc/desc.", false, "DIR" },
          CommandArgSpec{ "bands", "Comma list of mood keys to include.", false, "LIST" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            if (auto guestLimit = cli::GetIntOption(args, { "guest-limit", "guestLimit" }))
            {
                params["guestLimit"] = *guestLimit;
            }
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                params["order"] = *order;
            }
            if (auto direction = cli::GetStringOption(args, { "direction" }))
            {
                params["direction"] = *direction;
            }
            if (auto bands = cli::GetStringOption(args, { "bands", "band" }))
            {
                params["bands"] = cli::SplitCommaSeparated(*bands);
            }
            return CommandPlan{ "guests.moods", params };
        },
        renderers::RenderGuestMoodSummary });

    specs.push_back(CommandSpec{
        "guests",
        { "pickup" },
        "Pick up a guest for relocation.",
        "Lifts a guest off the map, entering 'picked' state. Guest must be walking or in another "
        "movable state. After pickup, use 'guests place' to set them down at a new location, or "
        "'guests drop' to return them to their original position.",
        { CommandArgSpec{ "id", "Guest identifier.", true, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            params["id"] = cli::RequireIntOption(args, { "id" }, "guest id");
            return CommandPlan{ "guests.pickup", params };
        },
        [](const json& result) { renderers::RenderGuestDetail(result); } });

    specs.push_back(CommandSpec{
        "guests",
        { "place" },
        "Place a picked-up guest at a new location.",
        "Sets down a guest who is currently in 'picked' state (from 'guests pickup') at tile --x --y. "
        "Height --z defaults to surface/path height if not specified. Fails if guest is not picked up "
        "or destination is invalid.",
        { CommandArgSpec{ "id", "Guest identifier.", true, "INT" },
          CommandArgSpec{ "x", "Destination tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Destination tile Y coordinate.", true, "INT" },
          CommandArgSpec{ "z", "Optional height in tile units (defaults to surface).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            params["id"] = cli::RequireIntOption(args, { "id" }, "guest id");
            params["x"] = cli::RequireIntOption(args, { "x" }, "tile x");
            params["y"] = cli::RequireIntOption(args, { "y" }, "tile y");
            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                params["z"] = *z;
            }
            return CommandPlan{ "guests.place", params };
        },
        [](const json& result) { renderers::RenderGuestDetail(result); } });

    specs.push_back(CommandSpec{
        "guests",
        { "drop" },
        "Cancel pickup and restore guest to original location.",
        "Returns a guest in 'picked' state to where they were before pickup. Use this to abort a "
        "relocation after 'guests pickup'. Only works if the guest is currently picked up.",
        { CommandArgSpec{ "id", "Guest identifier.", true, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            params["id"] = cli::RequireIntOption(args, { "id" }, "guest id");
            return CommandPlan{ "guests.drop", params };
        },
        [](const json& result) { renderers::RenderGuestDetail(result); } });
}

} // namespace rctctl::commands

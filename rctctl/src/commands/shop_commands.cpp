#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/rides.hpp"
#include "rctctl/renderers/shops.hpp"

#include <algorithm>
#include <cctype>
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

void AppendShopCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "shops",
        { "catalog" },
        "List available shop/stall blueprints.",
        "Shows every loaded shop/stall object identifier, ride type, stocked items, and build cost. Once placed, use 'rides get' to inspect shop status and pricing.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "shops.catalog", json::object() };
        },
        renderers::RenderShopCatalog });

    specs.push_back(CommandSpec{
        "shops",
        { "list" },
        "List operating shops and stalls.",
        "Shows every placed shop/stall with coordinates, status, pricing, and current performance. Note: Shops are internally classified as rides, so they also appear in 'rides list'. Use 'rides get --id <id>' for detailed inspection.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "shops.list", json::object() };
        },
        renderers::RenderShopList });

    specs.push_back(CommandSpec{
        "shops",
        { "place" },
        "Place a shop or stall.",
        "Creates a shop/stall building at --x/--y using --name, --entry-index, or --type. PLACEMENT REQUIREMENTS: Shops must be placed on owned land adjacent to a path. The shop automatically faces toward the adjacent path. Height auto-aligns to the path level unless --z is specified; if --z is provided, there must be an adjacent path at that height.",
        { CommandArgSpec{ "type", "Object identifier (slug, legacy id, or entry index).", false, "ID" },
          CommandArgSpec{ "name", "Display name from the catalog (e.g., \"Drinks Stall\").", false, "STRING" },
          CommandArgSpec{ "entry-index", "Entry index from shops catalog / rides available.", false, "INT" },
          CommandArgSpec{ "x", "Tile X coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "z", "Height in tile units (auto-detected from adjacent path if omitted).", false, "INT" },
          CommandArgSpec{ "dry-run", "Validate placement and estimate cost without actually building. Returns feasibility, estimated cost, and placement details. The camera will pan to the target location.", false, "BOOL" } },
        [](const ParsedArgs& args) {
            json params = json::object();

            auto appendEntryIndex = [&](int value) {
                if (value >= 0)
                {
                    params["entryIndex"] = value;
                }
            };

            auto hasSelector = false;
            if (auto type = cli::GetStringOption(args, { "type" }))
            {
                hasSelector = true;
                const auto& typeValue = *type;
                const bool numeric = !typeValue.empty()
                    && std::all_of(typeValue.begin(), typeValue.end(), [](unsigned char ch) { return std::isdigit(ch); });
                if (numeric)
                {
                    appendEntryIndex(cli::ParseIntValue(typeValue, "type"));
                }
                else
                {
                    params["type"] = typeValue;
                }
            }
            if (auto name = cli::GetStringOption(args, { "name" }))
            {
                hasSelector = true;
                params["name"] = *name;
            }
            if (auto entryIndex = cli::GetIntOption(args, { "entry-index", "entryIndex" }))
            {
                hasSelector = true;
                appendEntryIndex(*entryIndex);
            }
            if (!hasSelector)
            {
                throw std::runtime_error("Invalid: Provide --name, --entry-index, or --type to choose a shop");
            }

            params["x"] = cli::RequireIntOption(args, { "x" }, "x");
            params["y"] = cli::RequireIntOption(args, { "y" }, "y");
            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                params["z"] = *z;
            }
            if (auto dryRun = cli::GetBoolOption(args, { "dry-run", "dryRun" }))
            {
                params["dryRun"] = *dryRun;
            }
            return CommandPlan{ "shops.place", params };
        },
        renderers::RenderShopPlacement });

    specs.push_back(CommandSpec{
        "shops",
        { "remove" },
        "Remove a shop or stall.",
        "Demolishes an existing shop/stall via --ride-id/--ride-name or by targeting a tile with --x/--y (optionally --z).",
        { CommandArgSpec{ "ride-id", "Ride id to demolish (see rides.list).", false, "INT" },
          CommandArgSpec{ "ride-name", "Ride name to demolish.", false, "STRING" },
          CommandArgSpec{ "x", "Tile X coordinate (use with --y).", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (use with --x).", false, "INT" },
          CommandArgSpec{ "z", "Optional height filter in tile units.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto rideId = cli::GetIntOption(args, { "ride-id", "rideId", "id" }))
            {
                params["rideId"] = *rideId;
            }
            if (auto rideName = cli::GetStringOption(args, { "ride-name", "ride", "name" }))
            {
                params["rideName"] = *rideName;
            }

            auto x = cli::GetIntOption(args, { "x" });
            auto y = cli::GetIntOption(args, { "y" });
            if (x)
            {
                params["x"] = *x;
            }
            if (y)
            {
                params["y"] = *y;
            }
            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                params["z"] = *z;
            }

            const bool hasRideSelector = params.contains("rideId") || params.contains("rideName");
            const bool hasX = params.contains("x");
            const bool hasY = params.contains("y");
            const bool hasTile = hasX && hasY;
            if (!hasRideSelector && !hasTile)
            {
                throw std::runtime_error("Invalid: Provide --ride-id/--ride-name or --x/--y to identify the shop");
            }
            if ((hasX && !hasY) || (!hasX && hasY))
            {
                throw std::runtime_error("Invalid: --x and --y must be provided together");
            }

            return CommandPlan{ "shops.remove", params };
        },
        renderers::RenderShopRemoval });

    specs.push_back(CommandSpec{
        "shops",
        { "open" },
        "Open a shop or stall to guests.",
        "Sets the shop/stall status to OPEN. Requires --id or --name.",
        { CommandArgSpec{ "id", "Shop identifier (numeric).", false, "ID" },
          CommandArgSpec{ "name", "Shop name (case-insensitive).", false, "NAME" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            cli::ApplyRideSelector(params, args);
            params["status"] = "open";
            return CommandPlan{ "shops.setStatus", params };
        },
        renderers::RenderRideStatusChange });

    specs.push_back(CommandSpec{
        "shops",
        { "close" },
        "Close a shop or stall.",
        "Sets the shop/stall status to CLOSED. Requires --id or --name.",
        { CommandArgSpec{ "id", "Shop identifier (numeric).", false, "ID" },
          CommandArgSpec{ "name", "Shop name (case-insensitive).", false, "NAME" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            cli::ApplyRideSelector(params, args);
            params["status"] = "closed";
            return CommandPlan{ "shops.setStatus", params };
        },
        renderers::RenderRideStatusChange });

    specs.push_back(CommandSpec{
        "shops",
        { "price" },
        "Show item prices for a shop or stall.",
        "Displays current prices for each item sold at the shop, along with default prices for comparison. Use 'shops price set' to change prices.",
        { CommandArgSpec{ "id", "Shop identifier (numeric).", false, "ID" },
          CommandArgSpec{ "name", "Shop name (case-insensitive).", false, "NAME" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            cli::ApplyRideSelector(params, args);
            return CommandPlan{ "shops.getPrice", params };
        },
        [](const json& result) { renderers::RenderShopPrice(result, false); } });

    specs.push_back(CommandSpec{
        "shops",
        { "price", "set" },
        "Set item price at a shop or stall.",
        "Changes the price of items sold at a shop. By default sets the primary item price. Use --secondary=true for shops that sell two items (e.g., Burger Bar sells burgers and chips).",
        { CommandArgSpec{ "id", "Shop identifier (numeric).", false, "ID" },
          CommandArgSpec{ "name", "Shop name (case-insensitive).", false, "NAME" },
          CommandArgSpec{ "value", "New price in dollars (e.g., 2.50).", true, "AMOUNT" },
          CommandArgSpec{ "secondary", "Set to true to change the secondary item price.", false, "BOOL" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            cli::ApplyRideSelector(params, args);
            params["price"] = cli::RequireDoubleOption(args, { "value", "price" }, "price");
            if (auto secondary = cli::GetBoolOption(args, { "secondary" }))
            {
                params["secondary"] = *secondary;
            }
            return CommandPlan{ "shops.setPrice", params };
        },
        [](const json& result) { renderers::RenderShopPrice(result, true); } });

    specs.push_back(CommandSpec{
        "shops",
        { "finances" },
        "Show financial metrics for shops and stalls.",
        "Lists shops with profit, income, running cost, and total profit. Opens the Shops and Stalls window with the matching column displayed.",
        { CommandArgSpec{ "order", "Sort by: profit, income, cost, or totalprofit.", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort order: asc or desc.", false, "DIR" },
          CommandArgSpec{ "limit", "Show only first N shops.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                params["order"] = *order;
            }
            if (auto dir = cli::GetStringOption(args, { "direction" }))
            {
                params["direction"] = *dir;
            }
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            return CommandPlan{ "shops.finances", params };
        },
        renderers::RenderShopFinances });

    specs.push_back(CommandSpec{
        "shops",
        { "performance" },
        "Show performance metrics for shops and stalls.",
        "Lists shops with popularity, satisfaction, and customer metrics. Opens the Shops and Stalls window with the matching column displayed.",
        { CommandArgSpec{ "order", "Sort by: popularity, satisfaction, totalcustomers, or customers.", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort order: asc or desc.", false, "DIR" },
          CommandArgSpec{ "limit", "Show only first N shops.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                params["order"] = *order;
            }
            if (auto dir = cli::GetStringOption(args, { "direction" }))
            {
                params["direction"] = *dir;
            }
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            return CommandPlan{ "shops.performance", params };
        },
        renderers::RenderShopPerformance });

    // =========================================================================
    // Facilities Commands (Kiosks, Toilets, ATMs, First Aid)
    // =========================================================================

    specs.push_back(CommandSpec{
        "facilities",
        { "list" },
        "List operating facilities.",
        "Shows kiosks, toilets, ATMs, and first aid stations with their status and performance metrics.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "facilities.list", json::object() };
        },
        renderers::RenderFacilitiesList });

    specs.push_back(CommandSpec{
        "facilities",
        { "finances" },
        "Show financial metrics for facilities.",
        "Lists facilities with profit, income, running cost, and total profit. Opens the Kiosks and Facilities window with the matching column displayed.",
        { CommandArgSpec{ "order", "Sort by: profit, income, cost, or totalprofit.", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort order: asc or desc.", false, "DIR" },
          CommandArgSpec{ "limit", "Show only first N facilities.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                params["order"] = *order;
            }
            if (auto dir = cli::GetStringOption(args, { "direction" }))
            {
                params["direction"] = *dir;
            }
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            return CommandPlan{ "facilities.finances", params };
        },
        renderers::RenderFacilityFinances });

    specs.push_back(CommandSpec{
        "facilities",
        { "performance" },
        "Show performance metrics for facilities.",
        "Lists facilities with popularity, satisfaction, and customer metrics. Opens the Kiosks and Facilities window with the matching column displayed.",
        { CommandArgSpec{ "order", "Sort by: popularity, satisfaction, totalcustomers, or customers.", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort order: asc or desc.", false, "DIR" },
          CommandArgSpec{ "limit", "Show only first N facilities.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                params["order"] = *order;
            }
            if (auto dir = cli::GetStringOption(args, { "direction" }))
            {
                params["direction"] = *dir;
            }
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            return CommandPlan{ "facilities.performance", params };
        },
        renderers::RenderFacilityPerformance });
}

} // namespace rctctl::commands

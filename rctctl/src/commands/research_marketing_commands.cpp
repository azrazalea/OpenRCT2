#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/research_marketing.hpp"

#include <nlohmann/json.hpp>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;
}

void AppendResearchMarketingCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "research",
        { "status" },
        "Show research status.",
        "Displays funding level, progress, upcoming discoveries, and category priorities.",
        { CommandArgSpec{ "queue-limit", "Limit items shown from the queue.", false, "INT" },
          CommandArgSpec{ "queue-category", "Comma list of queue categories to include.", false, "LIST" },
          CommandArgSpec{ "queue-order", "Order queue by scenario, name, or category.", false, "FIELD" },
          CommandArgSpec{ "queue-direction", "Sort order asc/desc (ignored for scenario order).", false, "DIR" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto limit = cli::GetIntOption(args, { "queue-limit", "queueLimit" }))
            {
                params["queueLimit"] = *limit;
            }
            if (auto categories = cli::GetStringOption(args, { "queue-category", "queueCategories" }))
            {
                params["queueCategories"] = cli::SplitCommaSeparated(*categories);
            }
            if (auto order = cli::GetStringOption(args, { "queue-order", "queueOrder" }))
            {
                params["queueOrder"] = *order;
            }
            if (auto direction = cli::GetStringOption(args, { "queue-direction", "queueDirection" }))
            {
                params["queueDirection"] = *direction;
            }
            return CommandPlan{ "research.status", params };
        },
        renderers::RenderResearchStatus });

    specs.push_back(CommandSpec{
        "research",
        { "set" },
        "Configure research funding/priorities.",
        "Uses research.set --funding <none|min|normal|max> --priorities <comma list>.",
        { CommandArgSpec{ "funding", "Funding level: none, min, normal, max.", false, "STRING" },
          CommandArgSpec{ "priorities",
                          "Comma list of categories: transport, gentle, rollercoaster, thrill, water, shop, scenery.",
                          false, "LIST" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto funding = cli::GetStringOption(args, { "funding" }))
            {
                params["funding"] = *funding;
            }
            if (auto priorities = cli::GetStringOption(args, { "priorities" }))
            {
                params["priorities"] = cli::SplitCommaSeparated(*priorities);
            }
            return CommandPlan{ "research.set", params };
        },
        renderers::RenderResearchStatus });

    specs.push_back(CommandSpec{
        "marketing",
        { "status" },
        "Show marketing campaigns.",
        "Lists active marketing campaigns from marketing.status.",
        { CommandArgSpec{ "order", "Order by weeks, type, or target.", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort order asc/desc.", false, "DIR" },
          CommandArgSpec{ "type", "Filter by campaign type (park, ride, freeRide, freeEntry, freeFood, halfPriceEntry).", false, "TYPE" },
          CommandArgSpec{ "limit", "Limit number of campaigns shown.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                params["order"] = *order;
            }
            if (auto direction = cli::GetStringOption(args, { "direction" }))
            {
                params["direction"] = *direction;
            }
            if (auto type = cli::GetStringOption(args, { "type" }))
            {
                params["type"] = *type;
            }
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                params["limit"] = *limit;
            }
            return CommandPlan{ "marketing.status", params };
        },
        renderers::RenderMarketingStatus });

    specs.push_back(CommandSpec{
        "marketing",
        { "launch" },
        "Start marketing campaign.",
        "Launches campaign via marketing.launch --type <type> [--ride/--ride-id | --item] --weeks N.\n"
        "Campaign types:\n"
        "  freeEntry     - Free park entry vouchers\n"
        "  halfPriceEntry - Half-price park entry vouchers\n"
        "  freeRide      - Free ride vouchers (requires --ride or --ride-id)\n"
        "  ride          - Ride advertisement (requires --ride or --ride-id)\n"
        "  freeFood      - Free food/drink vouchers (requires --item)\n"
        "  park          - General park advertisement",
        { CommandArgSpec{ "type", "Campaign type (freeEntry, halfPriceEntry, freeRide, ride, freeFood, park).", true, "TYPE" },
          CommandArgSpec{ "ride", "Target ride name.", false, "STRING" },
          CommandArgSpec{ "ride-id", "Target ride id.", false, "INT" },
          CommandArgSpec{ "item", "Shop item (for free food).", false, "STRING" },
          CommandArgSpec{ "weeks", "Duration in weeks.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            params["type"] = cli::RequireStringOption(args, { "type" }, "type");
            if (auto ride = cli::GetStringOption(args, { "ride" }))
            {
                params["rideName"] = *ride;
            }
            if (auto rideId = cli::GetIntOption(args, { "ride-id" }))
            {
                params["rideId"] = *rideId;
            }
            if (auto item = cli::GetStringOption(args, { "item" }))
            {
                params["item"] = *item;
            }
            if (auto weeks = cli::GetIntOption(args, { "weeks" }))
            {
                params["weeks"] = *weeks;
            }
            return CommandPlan{ "marketing.launch", params };
        },
        renderers::RenderMarketingStatus });
}

} // namespace rctctl::commands

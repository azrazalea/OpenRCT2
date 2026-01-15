#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/amenities.hpp"

#include <nlohmann/json.hpp>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;
}

void AppendAmenityCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "amenities",
        { "analyze" },
        "Analyze park paths and generate placement heat map.",
        "Examines all paths in the park to identify optimal locations for benches, trash bins, and other amenities. "
        "Considers: (1) Distance from existing amenities (spread them out), (2) Proximity to nauseous ride exits "
        "(guests need benches), (3) Path distance from food shops (guests need trash bins). Returns statistics "
        "about path coverage and top placement candidates.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "amenities.analyze", json::object() };
        },
        renderers::RenderAmenityAnalysis });

    specs.push_back(CommandSpec{
        "amenities",
        { "estimate" },
        "Estimate how many amenities the park needs.",
        "Calculates recommended quantities for benches, trash bins, bathrooms, and first aid based on park size, "
        "guest capacity, ride nausea levels, and food shop locations. Compares current counts against estimates "
        "and provides recommendations for what to add.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "amenities.estimate", json::object() };
        },
        renderers::RenderAmenityEstimate });

    specs.push_back(CommandSpec{
        "amenities",
        { "place", "benches" },
        "Place benches at optimal locations.",
        "Automatically places benches at the best locations based on the heat map analysis. Prioritizes areas near "
        "nauseous ride exits and far from existing benches. Benches help guests rest and recover from ride nausea.",
        { CommandArgSpec{ "count", "Number of benches to place (default: 5).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto count = cli::GetIntOption(args, { "count", "n" }))
            {
                params["count"] = *count;
            }
            return CommandPlan{ "amenities.placeBenches", params };
        },
        renderers::RenderAmenityPlacement });

    specs.push_back(CommandSpec{
        "amenities",
        { "place", "bins" },
        "Place trash bins at optimal locations.",
        "Automatically places trash bins at the best locations based on the heat map analysis. Prioritizes areas "
        "within walking distance of food shops where guests are likely to have trash. Also considers nauseous ride "
        "exits. Bins help maintain park cleanliness rating.",
        { CommandArgSpec{ "count", "Number of bins to place (default: 5).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto count = cli::GetIntOption(args, { "count", "n" }))
            {
                params["count"] = *count;
            }
            return CommandPlan{ "amenities.placeBins", params };
        },
        renderers::RenderAmenityPlacement });

    specs.push_back(CommandSpec{
        "amenities",
        { "place", "bathrooms" },
        "Place bathroom buildings at optimal locations.",
        "Automatically places bathroom (toilet) buildings adjacent to paths at optimal locations. Bathrooms are "
        "buildings that require clear space adjacent to paths. Prioritizes areas near nauseous ride exits and food "
        "shops where guests are most likely to need facilities.",
        { CommandArgSpec{ "count", "Number of bathrooms to place (default: 1).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto count = cli::GetIntOption(args, { "count", "n" }))
            {
                params["count"] = *count;
            }
            return CommandPlan{ "amenities.placeBathrooms", params };
        },
        renderers::RenderAmenityPlacement });

    specs.push_back(CommandSpec{
        "amenities",
        { "place", "firstaid" },
        "Place first aid buildings at optimal locations.",
        "Automatically places first aid buildings adjacent to paths at optimal locations. First aid helps guests "
        "recover from extreme nausea. Prioritizes areas near highly nauseous ride exits.",
        { CommandArgSpec{ "count", "Number of first aid buildings to place (default: 1).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto count = cli::GetIntOption(args, { "count", "n" }))
            {
                params["count"] = *count;
            }
            return CommandPlan{ "amenities.placeFirstAid", params };
        },
        renderers::RenderAmenityPlacement });

    specs.push_back(CommandSpec{
        "amenities",
        { "recommend" },
        "Generate a placement plan without executing it.",
        "Analyzes the park and recommends a plan for placing amenities within the specified budget. The plan "
        "considers benches, bins, and bathrooms. Use this to preview what would be placed before committing. "
        "Each placement includes the reasoning for why that location was chosen.",
        {
            CommandArgSpec{ "budget", "Budget in dollars for amenity placement (required).", true, "INT" },
            CommandArgSpec{ "priority", "Comma-separated priority order (default: benches,bins,bathrooms).", false, "STRING" },
            CommandArgSpec{ "maxBenches", "Maximum number of benches to place (default: 50).", false, "INT" },
            CommandArgSpec{ "maxBins", "Maximum number of bins to place (default: 50).", false, "INT" },
            CommandArgSpec{ "maxBathrooms", "Maximum number of bathrooms to place (default: 5).", false, "INT" }
        },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto budget = cli::GetIntOption(args, { "budget", "b" }))
            {
                params["budget"] = *budget;
            }
            if (auto priority = cli::GetStringOption(args, { "priority", "p" }))
            {
                // Parse comma-separated priority list
                json priorities = json::array();
                std::string priorityStr = *priority;
                size_t pos = 0;
                while ((pos = priorityStr.find(',')) != std::string::npos)
                {
                    std::string item = priorityStr.substr(0, pos);
                    // Trim whitespace
                    item.erase(0, item.find_first_not_of(" \t"));
                    item.erase(item.find_last_not_of(" \t") + 1);
                    if (!item.empty())
                        priorities.push_back(item);
                    priorityStr.erase(0, pos + 1);
                }
                // Don't forget the last item
                priorityStr.erase(0, priorityStr.find_first_not_of(" \t"));
                priorityStr.erase(priorityStr.find_last_not_of(" \t") + 1);
                if (!priorityStr.empty())
                    priorities.push_back(priorityStr);
                if (!priorities.empty())
                    params["priority"] = priorities;
            }
            if (auto maxBenches = cli::GetIntOption(args, { "maxBenches" }))
            {
                params["maxBenches"] = *maxBenches;
            }
            if (auto maxBins = cli::GetIntOption(args, { "maxBins" }))
            {
                params["maxBins"] = *maxBins;
            }
            if (auto maxBathrooms = cli::GetIntOption(args, { "maxBathrooms" }))
            {
                params["maxBathrooms"] = *maxBathrooms;
            }
            return CommandPlan{ "amenities.recommend", params };
        },
        renderers::RenderAmenityRecommend });

    specs.push_back(CommandSpec{
        "amenities",
        { "auto" },
        "Automatically place optimal amenities within budget.",
        "THE RECOMMENDED COMMAND. Analyzes the park and automatically places benches, bins, and bathrooms at "
        "optimal locations within the specified budget. This is the intelligent one-shot command that does "
        "everything: analyzes paths, finds nauseous rides, locates food shops, calculates heat maps, and "
        "places amenities where they'll have the most impact. Perfect for quickly improving guest satisfaction.",
        {
            CommandArgSpec{ "budget", "Budget in dollars for amenity placement (required).", true, "INT" },
            CommandArgSpec{ "priority", "Comma-separated priority order (default: benches,bins,bathrooms).", false, "STRING" },
            CommandArgSpec{ "maxBenches", "Maximum number of benches to place (default: 50).", false, "INT" },
            CommandArgSpec{ "maxBins", "Maximum number of bins to place (default: 50).", false, "INT" },
            CommandArgSpec{ "maxBathrooms", "Maximum number of bathrooms to place (default: 5).", false, "INT" }
        },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto budget = cli::GetIntOption(args, { "budget", "b" }))
            {
                params["budget"] = *budget;
            }
            if (auto priority = cli::GetStringOption(args, { "priority", "p" }))
            {
                // Parse comma-separated priority list
                json priorities = json::array();
                std::string priorityStr = *priority;
                size_t pos = 0;
                while ((pos = priorityStr.find(',')) != std::string::npos)
                {
                    std::string item = priorityStr.substr(0, pos);
                    item.erase(0, item.find_first_not_of(" \t"));
                    item.erase(item.find_last_not_of(" \t") + 1);
                    if (!item.empty())
                        priorities.push_back(item);
                    priorityStr.erase(0, pos + 1);
                }
                priorityStr.erase(0, priorityStr.find_first_not_of(" \t"));
                priorityStr.erase(priorityStr.find_last_not_of(" \t") + 1);
                if (!priorityStr.empty())
                    priorities.push_back(priorityStr);
                if (!priorities.empty())
                    params["priority"] = priorities;
            }
            if (auto maxBenches = cli::GetIntOption(args, { "maxBenches" }))
            {
                params["maxBenches"] = *maxBenches;
            }
            if (auto maxBins = cli::GetIntOption(args, { "maxBins" }))
            {
                params["maxBins"] = *maxBins;
            }
            if (auto maxBathrooms = cli::GetIntOption(args, { "maxBathrooms" }))
            {
                params["maxBathrooms"] = *maxBathrooms;
            }
            return CommandPlan{ "amenities.auto", params };
        },
        renderers::RenderAmenityAuto });
}

} // namespace rctctl::commands

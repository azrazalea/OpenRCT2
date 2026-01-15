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
        "Examines all paths in the park to identify optimal locations for benches and trash bins. "
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
        { "auto" },
        "Automatically place optimal amenities within budget.",
        "Analyzes the park and automatically places benches and bins at optimal locations within the specified "
        "budget. Finds nauseous rides, locates food shops, calculates heat maps, and places amenities where "
        "they'll have the most impact. Benches are placed near ride exits for nauseous guests, bins near food "
        "shops for litter. Uses spreading penalty to distribute amenities evenly.",
        {
            CommandArgSpec{ "budget", "Budget in dollars for amenity placement (required).", true, "INT" },
            CommandArgSpec{ "maxBenches", "Maximum number of benches to place (default: 50).", false, "INT" },
            CommandArgSpec{ "maxBins", "Maximum number of bins to place (default: 50).", false, "INT" }
        },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto budget = cli::GetIntOption(args, { "budget", "b" }))
            {
                params["budget"] = *budget;
            }
            if (auto maxBenches = cli::GetIntOption(args, { "maxBenches" }))
            {
                params["maxBenches"] = *maxBenches;
            }
            if (auto maxBins = cli::GetIntOption(args, { "maxBins" }))
            {
                params["maxBins"] = *maxBins;
            }
            return CommandPlan{ "amenities.auto", params };
        },
        renderers::RenderAmenityAuto });
}

} // namespace rctctl::commands

#include "rctctl/renderers/research_marketing.hpp"

#include "rctctl/renderers/text.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;

std::string DescribeStage(const json& stageValue)
{
    // Handle both string format (new) and numeric format (old/compatibility)
    if (stageValue.is_string())
    {
        std::string stage = stageValue.get<std::string>();
        if (stage == "initialResearch")
            return "Initial research";
        if (stage == "designing")
            return "Designing";
        if (stage == "completingDesign")
            return "Completing design";
        if (stage == "allComplete")
            return "All complete";
        return "Unknown";
    }
    if (stageValue.is_number())
    {
        int stage = stageValue.get<int>();
        switch (stage)
        {
            case 0: return "Initial research";
            case 1: return "Designing";
            case 2: return "Completing design";
            case 255: return "All complete";
            default: return "Unknown";
        }
    }
    return "Unknown";
}
}

void RenderResearchStatus(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Research");
    canvas.KeyValue("Funding", result.value("fundingLevel", std::string("")));

    auto stageIt = result.find("progressStage");
    if (stageIt != result.end())
    {
        canvas.KeyValue("Stage", DescribeStage(*stageIt));
    }

    const auto& priorities = result.value("priorities", json::object());
    if (!priorities.empty())
    {
        canvas.Section("Priorities");
        for (auto it = priorities.begin(); it != priorities.end(); ++it)
        {
            std::string label = it.key();
            if (it.value().is_boolean() && it.value().get<bool>())
            {
                label += " (focused)";
            }
            canvas.Bullet(label);
        }
    }

    // Current research - what we show depends on visibility stage
    if (auto nextIt = result.find("next"); nextIt != result.end())
    {
        const auto& next = *nextIt;
        std::string status = next.value("status", std::string(""));

        if (status == "unknown")
        {
            canvas.KeyValue("Current research", "Unknown");
        }
        else if (status == "designing")
        {
            // Only category is visible
            canvas.KeyValue("Current research", next.value("category", std::string("Unknown category")));
        }
        else if (status == "completingDesign")
        {
            // Full name is visible
            std::string name = next.value("name", std::string(""));
            std::string category = next.value("category", std::string(""));
            if (!name.empty())
            {
                canvas.KeyValue("Current research", name + " (" + category + ")");
            }
            else
            {
                canvas.KeyValue("Current research", category);
            }
        }
    }

    // Expected completion
    if (result.contains("expectedMonth") && result.contains("expectedDay"))
    {
        std::ostringstream expected;
        expected << "Day " << result.value("expectedDay", 0) << ", Month " << (result.value("expectedMonth", 0) + 1);
        canvas.KeyValue("Expected", expected.str());
    }

    // Last completed research
    if (auto lastIt = result.find("last"); lastIt != result.end())
    {
        canvas.KeyValue("Last discovery", lastIt->value("name", std::string("")));
    }

    if (result.value("allComplete", false))
    {
        canvas.Paragraph("All research complete.");
    }
    else
    {
        int remaining = result.value("uninventedCount", 0);
        if (remaining > 0)
        {
            canvas.KeyValue("Items remaining", remaining);
        }
    }
}

void RenderMarketingStatus(const json& result)
{
    const auto& active = result.value("active", json::array());
    if (active.empty())
    {
        TextCanvas canvas(std::cout);
        canvas.Section("Marketing");
        canvas.Paragraph("No active marketing campaigns.");
        return;
    }
    TextCanvas canvas(std::cout);
    canvas.Section("Marketing");
    TableView table;
    table.headers = { "Type", "Target", "Weeks Left" };
    for (const auto& campaign : active)
    {
        std::string target;
        if (campaign.contains("rideName"))
        {
            target = campaign.value("rideName", std::string(""));
        }
        else if (campaign.contains("shopItem"))
        {
            target = campaign.value("shopItem", std::string(""));
        }
        else
        {
            target = campaign.value("target", std::string("park"));
        }
        table.rows.push_back({ campaign.value("type", std::string("campaign")), target,
            std::to_string(campaign.value("weeksLeft", 0)) });
    }
    canvas.Table(table);
}

} // namespace rctctl::renderers

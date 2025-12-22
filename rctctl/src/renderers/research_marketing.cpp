#include "rctctl/renderers/research_marketing.hpp"

#include "rctctl/renderers/text.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;

std::string FormatPercent(double value)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << value << "%";
    return oss.str();
}

double ExtractProgressPercent(const json& result)
{
    if (auto it = result.find("progressPercent"); it != result.end() && it->is_number())
    {
        return it->get<double>();
    }
    double raw = result.value("progress", 0.0);
    int stage = result.value("progressStage", 0);
    if (stage == 4)
    {
        return 100.0;
    }
    if (stage == 0)
    {
        return 0.0;
    }
    constexpr double kProgressUnits = 65535.0;
    double percent = (raw / kProgressUnits) * 100.0;
    if (percent < 0.0)
    {
        percent = 0.0;
    }
    if (percent > 100.0)
    {
        percent = 100.0;
    }
    return percent;
}

const char* DescribeStage(int stage)
{
    switch (stage)
    {
        case 0:
            return "Initial research";
        case 1:
            return "Designing";
        case 2:
            return "Completing design";
        case 3:
            return "Unknown";
        case 4:
            return "Finished";
        default:
            return "Unknown";
    }
}
}

void RenderResearchStatus(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Research");
    canvas.KeyValue("Funding", result.value("fundingLevel", std::string("")));
    canvas.KeyValue("Stage", DescribeStage(result.value("progressStage", 0)));
    canvas.KeyValue("Progress", FormatPercent(ExtractProgressPercent(result)));

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

    if (auto nextIt = result.find("next"); nextIt != result.end())
    {
        canvas.KeyValue("Next discovery", nextIt->value("name", std::string("")));
    }
    if (auto lastIt = result.find("last"); lastIt != result.end())
    {
        canvas.KeyValue("Last discovery", lastIt->value("name", std::string("")));
    }

    if (result.value("allComplete", false))
    {
        canvas.Paragraph("All research complete.");
    }

    if (auto queueIt = result.find("queue"); queueIt != result.end() && queueIt->is_array())
    {
        const auto& queue = *queueIt;
        if (!queue.empty())
        {
            canvas.Section("Queue");
            canvas.KeyValue("Entries", static_cast<int>(queue.size()));
            TableView table;
            table.headers = { "Name", "Category", "Type" };
            for (const auto& entry : queue)
            {
                table.rows.push_back({ entry.value("name", std::string("")),
                    entry.value("category", entry.value("categoryKey", std::string(""))),
                    entry.value("type", std::string("")) });
            }
            canvas.Table(table);
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

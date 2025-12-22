#include "rctctl/renderers/guests.hpp"

#include "rctctl/renderers/text.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;

std::string JoinGuestSamples(const json& samples)
{
    if (!samples.is_array() || samples.empty())
    {
        return "-";
    }
    std::ostringstream oss;
    bool first = true;
    for (const auto& guest : samples)
    {
        if (!first)
        {
            oss << ", ";
        }
        first = false;
        oss << guest.value("name", std::string("Guest")) << " (id:" << guest.value("id", -1) << ")";
    }
    return oss.str();
}
}

void RenderGuestList(const json& result)
{
    const auto& guests = result.value("guests", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Guests");
    canvas.KeyValue("Shown", static_cast<int>(guests.size()));
    if (auto matchedIt = result.find("matchedGuests"); matchedIt != result.end() && matchedIt->is_number_integer())
    {
        canvas.KeyValue("Matched", matchedIt->get<int>());
    }
    if (auto totalIt = result.find("totalGuests"); totalIt != result.end() && totalIt->is_number_integer())
    {
        canvas.KeyValue("Guests in park", totalIt->get<int>());
    }
    if (auto cursorIt = result.find("nextCursor"); cursorIt != result.end())
    {
        if (cursorIt->is_number_integer())
        {
            canvas.KeyValue("Next cursor (--after)", cursorIt->get<int>());
        }
        else if (result.value("hasMore", false))
        {
            canvas.KeyValue("More results", std::string("available (supply --after with last shown ID)"));
        }
    }
    else if (result.value("hasMore", false))
    {
        canvas.KeyValue("More results", std::string("available (supply --after with last shown ID)"));
    }

    TableView table;
    table.headers = { "ID", "State", "Name" };
    for (const auto& guest : guests)
    {
        table.rows.push_back({ std::to_string(guest.value("id", -1)), guest.value("state", std::string("")),
            guest.value("name", std::string("Guest")) });
    }
    canvas.Table(table);
}

void RenderGuestDetail(const json& guest)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Guest Detail");
    canvas.KeyValue("ID", guest.value("id", -1));
    canvas.KeyValue("Name", guest.value("name", std::string("Guest")));
    canvas.KeyValue("State", guest.value("state", std::string("")));

    const auto& coords = guest.value("coords", json::object());
    std::ostringstream location;
    location << '(' << coords.value("x", 0) << ", " << coords.value("y", 0) << ", z" << coords.value("z", 0)
             << ')';
    canvas.KeyValue("Location", location.str());

    const auto& needs = guest.value("needs", json::object());
    canvas.Section("Needs");
    canvas.KeyValue("Happiness", needs.value("happiness", 0));
    canvas.KeyValue("Hunger", needs.value("hunger", 0));
    canvas.KeyValue("Thirst", needs.value("thirst", 0));
    canvas.KeyValue("Nausea", needs.value("nausea", 0));
    canvas.KeyValue("Energy", needs.value("energy", 0));
    canvas.KeyValue("Toilet", needs.value("toilet", 0));

    if (guest.contains("itemsCarried"))
    {
        const auto& items = guest["itemsCarried"];
        canvas.Section("Inventory");
        if (items.empty())
        {
            canvas.Paragraph("No carried items.");
        }
        else
        {
            for (const auto& item : items)
            {
                std::ostringstream line;
                line << item.value("label", std::string("item"));
                auto category = item.value("category", std::string());
                if (!category.empty())
                {
                    line << " (" << category << ')';
                }
                canvas.Bullet(line.str());
            }
        }
    }
}

void RenderGuestThoughtSummary(const json& payload)
{
    const auto& groups = payload.value("groups", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Guest Thoughts");

    auto totalGroups = payload.value("totalGroups", static_cast<int>(groups.size()));
    auto offset = payload.value("offset", 0);
    auto hasMore = payload.value("hasMore", false);

    std::ostringstream shownInfo;
    shownInfo << groups.size() << " of " << totalGroups;
    if (offset > 0)
    {
        shownInfo << " (offset " << offset << ")";
    }
    canvas.KeyValue("Shown", shownInfo.str());
    canvas.KeyValue("Guests considered", payload.value("consideredGuests", 0));
    canvas.KeyValue("Guests in park", payload.value("totalGuests", 0));
    if (hasMore)
    {
        canvas.KeyValue("Next page", std::string("--offset ") + std::to_string(payload.value("nextOffset", 0)));
    }

    if (groups.empty())
    {
        if (offset > 0)
        {
            canvas.Paragraph("No more thought groups at this offset.");
        }
        else
        {
            canvas.Paragraph("No common thoughts are currently tracked.");
        }
        return;
    }

    TableView table;
    table.headers = { "Thought", "Guests", "Sample" };
    for (const auto& group : groups)
    {
        auto label = group.value("text", std::string());
        if (group.contains("rideName"))
        {
            label += " — " + group.value("rideName", std::string());
        }
        table.rows.push_back({ label, std::to_string(group.value("count", 0)),
            JoinGuestSamples(group.value("guestSamples", json::array())) });
    }
    canvas.Table(table);
}

void RenderGuestMoodSummary(const json& payload)
{
    const auto& groups = payload.value("groups", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Guest Moods");
    canvas.KeyValue("Groups", static_cast<int>(groups.size()));
    canvas.KeyValue("Guests considered", payload.value("consideredGuests", 0));

    if (groups.empty())
    {
        canvas.Paragraph("No guests are in the park.");
        return;
    }

    TableView table;
    table.headers = { "Mood", "Guests", "Avg happiness", "Range", "Sample" };
    for (const auto& group : groups)
    {
        std::ostringstream avg;
        avg << std::fixed << std::setprecision(1) << group.value("avgHappiness", 0.0);
        const auto& range = group.value("range", json::object());
        std::ostringstream band;
        band << range.value("min", 0) << "-" << range.value("max", 0);
        table.rows.push_back({ group.value("label", group.value("key", std::string("mood"))),
            std::to_string(group.value("count", 0)), avg.str(), band.str(),
            JoinGuestSamples(group.value("guestSamples", json::array())) });
    }
    canvas.Table(table);
}

} // namespace rctctl::renderers

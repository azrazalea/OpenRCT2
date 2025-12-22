#include "rctctl/renderers/park.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"
#include "rctctl/util/string_utils.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;
}

void RenderParkStatus(const json& result)
{
    const auto& scenario = result.value("scenario", json::object());
    const auto& objective = result.value("objective", json::object());
    const auto& date = result.value("date", json::object());

    TextCanvas canvas(std::cout);

    canvas.Section("Park Overview");
    canvas.KeyValue("Name", result.value("name", std::string("(unnamed)")));
    if (!scenario.empty())
    {
        canvas.KeyValue("Scenario", scenario.value("name", std::string("(unknown scenario)")));
        auto goal = scenario.value("goalSummary", std::string());
        if (goal.empty())
        {
            goal = objective.value("summary", std::string());
        }
        if (!goal.empty())
        {
            canvas.Paragraph(goal);
        }
    }

    const bool isOpen = result.value("isOpen", false);
    const auto guests = result.value("guests", 0);
    const auto heading = result.value("guestsHeading", 0);
    const auto rating = result.value("parkRating", 0);
    canvas.KeyValue("Park", isOpen ? "OPEN" : "CLOSED");
    std::ostringstream guestsLine;
    guestsLine << guests << " (" << heading << " heading)";
    canvas.KeyValue("Guests", guestsLine.str());
    canvas.KeyValue("Rating", rating);
    canvas.KeyValue("Date", util::FormatDateString(date));

    canvas.Section("Finances");
    canvas.KeyValue("Entrance fee", util::FormatCurrency(result.value("entranceFee", 0.0)));
    canvas.KeyValue("Cash", util::FormatCurrency(result.value("cash", 0.0)));
    std::ostringstream loanLine;
    loanLine << util::FormatCurrency(result.value("loan", 0.0)) << " / "
             << util::FormatCurrency(result.value("loanMax", 0.0));
    canvas.KeyValue("Loan", loanLine.str());
    canvas.KeyValue("Park value", util::FormatCurrency(result.value("parkValue", 0.0)));
    canvas.KeyValue("Company value", util::FormatCurrency(result.value("companyValue", 0.0)));

    // Display spatial layout information
    const auto& spatial = result.value("spatial", json::object());
    if (!spatial.empty())
    {
        canvas.Section("Spatial Layout");

        // Park boundary bounding box (tile coordinates)
        const auto& boundary = spatial.value("boundary", json::object());
        if (!boundary.empty())
        {
            std::ostringstream boundaryLine;
            boundaryLine << "(" << boundary.value("minX", 0) << ", " << boundary.value("minY", 0) << ") to ("
                         << boundary.value("maxX", 0) << ", " << boundary.value("maxY", 0) << ") tiles";
            canvas.KeyValue("Park boundary", boundaryLine.str());
            canvas.KeyValue("Park area", std::to_string(spatial.value("areaInTiles", 0)) + " tiles");
        }

        // Entrances
        const auto& entrances = spatial.value("entrances", json::array());
        if (!entrances.empty())
        {
            for (const auto& entrance : entrances)
            {
                std::ostringstream entranceLine;
                entranceLine << "(" << entrance.value("x", 0) << ", " << entrance.value("y", 0) << ", "
                             << entrance.value("z", 0) << ") facing " << entrance.value("facing", std::string("unknown"));
                canvas.KeyValue("Entrance " + std::to_string(entrance.value("index", 0) + 1), entranceLine.str());
            }
        }
    }

    // Display recent news count
    const auto& recentNews = result.value("recentNews", json::array());
    if (!recentNews.empty())
    {
        std::ostringstream newsLine;
        newsLine << recentNews.size() << " update" << (recentNews.size() == 1 ? "" : "s");
        canvas.KeyValue("Recent News", newsLine.str());
    }
}

void RenderParkGuests(const json& result)
{
    const auto guests = result.value("count", result.value("guests", 0));
    const auto heading = result.value("headingToPark", result.value("guestsHeading", 0));
    const auto rating = result.value("parkRating", 0);
    const bool isOpen = result.value("isOpen", false);

    TextCanvas canvas(std::cout);
    canvas.Section("Guests");
    canvas.KeyValue("In park", guests);
    canvas.KeyValue("Heading", heading);
    canvas.KeyValue("Rating", rating);
    canvas.KeyValue("Park", isOpen ? "OPEN" : "CLOSED");
}

void RenderParkPrice(const json& result, bool announceChange)
{
    double fee = result.value("entranceFee", 0.0);
    bool freeEntry = result.value("isFreeEntry", fee <= 0.0);
    bool open = result.value("parkOpen", false);

    TextCanvas canvas(std::cout);
    canvas.Section("Admission");
    std::ostringstream priceLine;
    priceLine << util::FormatCurrency(fee);
    if (freeEntry)
    {
        priceLine << " (free entry)";
    }
    canvas.KeyValue(announceChange ? "Updated fee" : "Entrance fee", priceLine.str());
    canvas.KeyValue("Park", open ? "OPEN" : "CLOSED");
}

void RenderParkGateState(const json& result)
{
    bool isOpen = result.value("isOpen", false);
    std::string previousState = result.value("previousState", "");
    TextCanvas canvas(std::cout);
    canvas.Section("Park");
    canvas.KeyValue("Status", isOpen ? "OPEN" : "CLOSED");
    if (!previousState.empty())
    {
        bool wasOpen = (previousState == "open");
        if (wasOpen != isOpen)
        {
            canvas.KeyValue("Changed from", wasOpen ? "open" : "closed");
        }
        else
        {
            canvas.Paragraph("(no change)");
        }
    }
}

void RenderParkRatingHistory(const json& result)
{
    const auto records = result.value("records", json::array());
    if (records.empty())
    {
        TextCanvas canvas(std::cout);
        canvas.Section("Park Rating");
        canvas.Paragraph("No rating history recorded yet.");
        return;
    }

    TableView table;
    table.headers = { "Month", "Rating" };
    for (const auto& record : records)
    {
        auto monthOffset = record.value("monthOffset", 0);
        auto rating = record.value("rating", 0);
        table.rows.push_back({ std::to_string(monthOffset), std::to_string(rating) });
    }

    TextCanvas canvas(std::cout);
    canvas.Section("Park Rating");
    canvas.Table(table);
}

void RenderParkRewards(const json& result)
{
    const auto awards = result.value("awards", json::array());
    if (awards.empty())
    {
        TextCanvas canvas(std::cout);
        canvas.Section("Awards");
        canvas.Paragraph("No active awards.");
        return;
    }

    TextCanvas canvas(std::cout);
    canvas.Section("Awards");
    int index = 1;
    for (const auto& award : awards)
    {
        auto label = util::StripFormatCodes(award.value("label", std::string()));
        if (label.empty())
        {
            label = award.value("type", std::string("award"));
        }
        bool positive = award.value("isPositive", true);
        auto remaining = award.value("expiresInMonths", 0);
        std::ostringstream line;
        line << index++ << ". " << label << " (" << (positive ? "positive" : "negative")
             << ") — expires in " << remaining << " month(s)";
        canvas.Paragraph(line.str());
    }
}

void RenderSandboxStatus(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Sandbox");
    canvas.KeyValue("Money rules", result.value("noMoney", false) ? "no-money" : "economy enabled");
    canvas.KeyValue("Unlock all prices", result.value("unlockAllPrices", false));
    const auto& cheats = result.value("cheats", json::object());
    for (auto it = cheats.begin(); it != cheats.end(); ++it)
    {
        if (it.value().is_boolean())
        {
            std::string line = std::string(it.key()) + ": " + (it.value().get<bool>() ? "on" : "off");
            canvas.Bullet(line);
        }
    }
}

void RenderParkWarnings(const json& result)
{
    const auto& warnings = result.value("warnings", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Warnings");
    for (const auto& entry : warnings)
    {
        auto key = entry.value("key", std::string("warning"));
        auto count = entry.value("count", 0);
        auto threshold = entry.value("threshold", 0);
        auto scaled = entry.value("scaledThreshold", 0);
        bool triggered = entry.value("triggered", false);
        std::ostringstream line;
        line << key << ": " << count;
        if (threshold > 0)
        {
            line << " (threshold " << std::max(threshold, scaled) << ")";
        }
        if (triggered)
        {
            line << " [TRIGGERED]";
        }
        canvas.Bullet(line.str());
    }

    canvas.KeyValue("Broken rides", result.value("ridesBroken", 0));
    if (auto queueHotspot = result.find("queueHotspot"); queueHotspot != result.end())
    {
        auto rideName = queueHotspot->value("rideName", std::string());
        std::ostringstream line;
        line << "Queue hotspot: ride #" << queueHotspot->value("rideId", -1);
        if (!rideName.empty())
        {
            line << " (" << rideName << ")";
        }
        line << " complaining guests " << queueHotspot->value("complaints", 0);
        canvas.Paragraph(line.str());
    }
}

} // namespace rctctl::renderers

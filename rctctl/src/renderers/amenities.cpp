#include "rctctl/renderers/amenities.hpp"

#include "rctctl/renderers/text.hpp"
#include "rctctl/util/format.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;
} // namespace

void RenderAmenityAnalysis(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Amenity Analysis");

    // Path statistics
    const auto& pathStats = result.value("pathStats", json::object());
    canvas.KeyValue("Total paths", pathStats.value("totalPaths", 0));
    canvas.KeyValue("Placeable tiles", pathStats.value("placeableTiles", 0));
    canvas.KeyValue("Junctions (blocked)", pathStats.value("junctions", 0));
    canvas.KeyValue("Queue lines (blocked)", pathStats.value("queueLines", 0));
    canvas.KeyValue("Building-blocked", pathStats.value("buildingBlocked", 0));
    canvas.KeyValue("Full (2+ additions)", pathStats.value("fullTiles", 0));

    // Existing amenities
    const auto& existing = result.value("existingAmenities", json::object());
    canvas.Paragraph("");
    canvas.Paragraph("Existing amenities:");
    canvas.KeyValue("  Benches", existing.value("benches", 0));
    canvas.KeyValue("  Trash bins", existing.value("bins", 0));
    canvas.KeyValue("  Lamps", existing.value("lamps", 0));
    canvas.KeyValue("  Other", existing.value("other", 0));

    // Nauseous ride exits
    const auto& nauseousExits = result.value("nauseousExits", json::array());
    if (!nauseousExits.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Nauseous ride exits (high priority for amenities):");
        TableView table;
        table.headers = { "Ride", "Nausea Rating", "Exit Location" };
        for (const auto& exit : nauseousExits)
        {
            std::ostringstream loc;
            loc << '(' << exit.value("x", 0) << ", " << exit.value("y", 0) << ')';
            std::ostringstream nausea;
            nausea << std::fixed << std::setprecision(2) << exit.value("nauseaRating", 0.0);
            table.rows.push_back({
                exit.value("rideName", std::string()),
                nausea.str(),
                loc.str()
            });
        }
        canvas.Table(table);
    }

    // Food shops
    const auto& foodShops = result.value("foodShops", json::array());
    if (!foodShops.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Food shops (bins needed nearby):");
        TableView table;
        table.headers = { "Shop", "Location" };
        for (const auto& shop : foodShops)
        {
            std::ostringstream loc;
            loc << '(' << shop.value("x", 0) << ", " << shop.value("y", 0) << ')';
            table.rows.push_back({
                shop.value("shopName", std::string()),
                loc.str()
            });
        }
        canvas.Table(table);
    }

    // Top placement candidates
    const auto& topCandidates = result.value("topCandidates", json::array());
    if (!topCandidates.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Top 10 amenity placement candidates:");
        TableView table;
        table.headers = { "Location", "Bench Score", "Bin Score" };
        for (const auto& candidate : topCandidates)
        {
            std::ostringstream loc;
            loc << '(' << candidate.value("x", 0) << ", " << candidate.value("y", 0)
                << ", z" << candidate.value("z", 0) << ')';
            std::ostringstream benchScore, binScore;
            benchScore << std::fixed << std::setprecision(1) << candidate.value("benchValue", 0.0);
            binScore << std::fixed << std::setprecision(1) << candidate.value("binValue", 0.0);
            table.rows.push_back({
                loc.str(),
                benchScore.str(),
                binScore.str()
            });
        }
        canvas.Table(table);
    }
}

void RenderAmenityEstimate(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Amenity Requirements Estimate");

    // Current counts
    const auto& current = result.value("current", json::object());
    canvas.Paragraph("Current amenities:");
    canvas.KeyValue("  Benches", current.value("benches", 0));
    canvas.KeyValue("  Trash bins", current.value("bins", 0));
    canvas.KeyValue("  Bathrooms", current.value("bathrooms", 0));
    canvas.KeyValue("  First aid", current.value("firstAid", 0));

    // Estimated needs
    const auto& estimated = result.value("estimated", json::object());
    canvas.Paragraph("");
    canvas.Paragraph("Estimated needs:");
    canvas.KeyValue("  Benches", estimated.value("benches", 0));
    canvas.KeyValue("  Trash bins", estimated.value("bins", 0));
    canvas.KeyValue("  Bathrooms", estimated.value("bathrooms", 0));
    canvas.KeyValue("  First aid", estimated.value("firstAid", 0));

    // Recommendations
    const auto& recommended = result.value("recommended", json::object());
    canvas.Paragraph("");
    canvas.Paragraph("Recommended to add:");
    int benchesToAdd = recommended.value("benches", 0);
    int binsToAdd = recommended.value("bins", 0);
    int bathroomsToAdd = recommended.value("bathrooms", 0);
    int firstAidToAdd = recommended.value("firstAid", 0);

    canvas.KeyValue("  Benches", benchesToAdd);
    canvas.KeyValue("  Trash bins", binsToAdd);
    canvas.KeyValue("  Bathrooms", bathroomsToAdd);
    canvas.KeyValue("  First aid", firstAidToAdd);

    // Warnings
    const auto& warnings = result.value("warnings", json::array());
    if (!warnings.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Warnings:");
        for (const auto& warning : warnings)
        {
            canvas.Paragraph("  - " + warning.get<std::string>());
        }
    }
}

void RenderAmenityPlacement(const json& result)
{
    TextCanvas canvas(std::cout);

    std::string amenityType = result.value("type", "amenities");
    canvas.Section(amenityType + " Placement");

    int placed = result.value("placedCount", 0);
    int requested = result.value("requestedCount", 0);
    canvas.KeyValue("Requested", requested);
    canvas.KeyValue("Placed", placed);

    const auto& placedItems = result.value("placed", json::array());
    if (!placedItems.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Successfully placed:");
        TableView table;
        table.headers = { "Location", "Score" };
        for (const auto& item : placedItems)
        {
            std::ostringstream loc;
            loc << '(' << item.value("x", 0) << ", " << item.value("y", 0)
                << ", z" << item.value("z", 0) << ')';
            table.rows.push_back({
                loc.str(),
                std::to_string(static_cast<int>(item.value("value", 0.0)))
            });
        }
        canvas.Table(table);
    }

    const auto& failedItems = result.value("failed", json::array());
    if (!failedItems.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Failed placements:");
        for (const auto& item : failedItems)
        {
            std::ostringstream msg;
            msg << "  (" << item.value("x", 0) << ", " << item.value("y", 0) << "): "
                << item.value("error", "unknown error");
            canvas.Paragraph(msg.str());
        }
    }

    if (placed < requested)
    {
        canvas.Paragraph("");
        canvas.Paragraph("Note: Not all placements succeeded. This may be due to:");
        canvas.Paragraph("  - Tiles already having maximum path additions");
        canvas.Paragraph("  - Adjacent buildings blocking placement");
        canvas.Paragraph("  - Other placement constraints");
    }
}

void RenderAmenityRecommend(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Amenity Placement Recommendation");

    std::string planId = result.value("planId", "");
    canvas.KeyValue("Plan ID", planId);

    int totalCost = result.value("totalCost", 0);
    int budget = result.value("budget", 0);
    int remaining = result.value("remainingBudget", 0);

    canvas.KeyValue("Budget", "$" + std::to_string(budget));
    canvas.KeyValue("Planned cost", "$" + std::to_string(totalCost));
    canvas.KeyValue("Remaining", "$" + std::to_string(remaining));

    std::string summary = result.value("summary", "");
    canvas.Paragraph("");
    canvas.Paragraph("Plan: " + summary);

    // Show counts
    const auto& counts = result.value("counts", json::object());
    canvas.Paragraph("");
    canvas.Paragraph("Planned amenities:");
    canvas.KeyValue("  Benches", counts.value("benches", 0));
    canvas.KeyValue("  Bins", counts.value("bins", 0));
    canvas.KeyValue("  Bathrooms", counts.value("bathrooms", 0));

    // Show placements with reasoning
    const auto& placements = result.value("placements", json::array());
    if (!placements.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Planned placements (with reasoning):");
        TableView table;
        table.headers = { "Type", "Location", "Score", "Reason" };
        for (const auto& p : placements)
        {
            std::ostringstream loc;
            loc << '(' << p.value("x", 0) << ", " << p.value("y", 0) << ')';
            table.rows.push_back({
                p.value("type", std::string()),
                loc.str(),
                std::to_string(static_cast<int>(p.value("score", 0.0))),
                p.value("reason", std::string())
            });
        }
        canvas.Table(table);
    }

    canvas.Paragraph("");
    canvas.Paragraph("This is a preview only. Use 'amenities auto --budget " +
                     std::to_string(budget) + "' to execute this plan.");
}

void RenderAmenityAuto(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Automatic Amenity Placement Complete");

    int totalSpent = result.value("totalSpent", 0);
    int budget = result.value("budget", 0);

    canvas.KeyValue("Budget", "$" + std::to_string(budget));
    canvas.KeyValue("Spent", "$" + std::to_string(totalSpent));

    std::string summary = result.value("summary", "");
    canvas.Paragraph("");
    canvas.Paragraph(summary);

    // Show counts
    const auto& counts = result.value("counts", json::object());
    canvas.Paragraph("");
    canvas.Paragraph("Placed amenities:");
    canvas.KeyValue("  Benches", counts.value("benches", 0));
    canvas.KeyValue("  Bins", counts.value("bins", 0));
    canvas.KeyValue("  Bathrooms", counts.value("bathrooms", 0));

    // Show successful placements with reasoning
    const auto& placed = result.value("placed", json::array());
    if (!placed.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Successfully placed:");
        TableView table;
        table.headers = { "Type", "Location", "Reason" };
        for (const auto& p : placed)
        {
            std::ostringstream loc;
            loc << '(' << p.value("x", 0) << ", " << p.value("y", 0) << ')';
            table.rows.push_back({
                p.value("type", std::string()),
                loc.str(),
                p.value("reason", std::string())
            });
        }
        canvas.Table(table);
    }

    // Show failures
    const auto& failed = result.value("failed", json::array());
    if (!failed.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("Failed placements:");
        for (const auto& f : failed)
        {
            std::ostringstream msg;
            msg << "  " << f.value("type", std::string()) << " at ("
                << f.value("x", 0) << ", " << f.value("y", 0) << "): "
                << f.value("error", "unknown error");
            canvas.Paragraph(msg.str());
        }
    }

    if (placed.empty())
    {
        canvas.Paragraph("");
        canvas.Paragraph("No amenities were placed. Possible reasons:");
        canvas.Paragraph("  - Budget too low");
        canvas.Paragraph("  - No suitable path tiles available");
        canvas.Paragraph("  - All optimal locations already have amenities");
    }
}

} // namespace rctctl::renderers

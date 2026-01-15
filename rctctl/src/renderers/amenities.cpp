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

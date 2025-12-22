#include "rctctl/renderers/map.hpp"

#include "rctctl/renderers/text.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace rctctl::renderers {
namespace {
using json = nlohmann::json;

// Convert edge bitmask to direction string
// OpenRCT2 uses isometric diagonal directions (see Paint.TileElement.h):
//   Bit 0 = NE (TOPRIGHT)    → connects to tile at X-1
//   Bit 1 = SE (BOTTOMRIGHT) → connects to tile at Y+1
//   Bit 2 = SW (BOTTOMLEFT)  → connects to tile at X+1
//   Bit 3 = NW (TOPLEFT)     → connects to tile at Y-1
std::string FormatEdges(int edges)
{
    if (edges == 0)
        return "none";
    std::string result;
    if (edges & 1)
        result += result.empty() ? "NE" : "+NE";
    if (edges & 2)
        result += result.empty() ? "SE" : "+SE";
    if (edges & 4)
        result += result.empty() ? "SW" : "+SW";
    if (edges & 8)
        result += result.empty() ? "NW" : "+NW";
    return result;
}
}

void RenderMapStatus(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Map");
    std::ostringstream size;
    size << result.value("width", 0) << 'x' << result.value("height", 0) << " tiles";
    canvas.KeyValue("Size", size.str());
    canvas.KeyValue("Owned", result.value("ownedTiles", 0));
    canvas.KeyValue("Rights", result.value("constructionRightsTiles", 0));
    canvas.KeyValue("Water", result.value("waterTiles", 0));
    std::ostringstream height;
    height << result.value("minHeight", 0) << "-" << result.value("maxHeight", 0);
    canvas.KeyValue("Height range", height.str());
}

void RenderMapTile(const json& result)
{
    if (!result.value("isValid", false))
    {
        TextCanvas canvas(std::cout);
        canvas.Section("Tile");
        canvas.Paragraph("Tile outside current map.");
        return;
    }

    TextCanvas canvas(std::cout);
    canvas.Section("Tile");
    std::ostringstream coords;
    coords << '(' << result.value("x", 0) << ", " << result.value("y", 0) << ')';
    canvas.KeyValue("Coords", coords.str());
    const auto& surface = result.value("surface", json::object());
    if (!surface.empty())
    {
        std::ostringstream surf;
        surf << "base " << surface.value("baseHeight", 0) << " | clearance "
             << surface.value("clearanceHeight", 0) << " | water " << surface.value("waterHeight", 0);
        canvas.KeyValue("Surface", surf.str());
    }

    const auto& elements = result.value("elements", json::array());
    if (!elements.empty())
    {
        canvas.Section("Elements");
        for (const auto& element : elements)
        {
            std::ostringstream line;
            line << element.value("type", std::string("tile")) << " @" << element.value("base", 0) << '-'
                 << element.value("clearance", 0);
            if (element.contains("surface"))
            {
                const auto& s = element["surface"];
                line << " slope=" << s.value("slope", 0);
            }
            if (element.contains("path"))
            {
                const auto& p = element["path"];
                if (p.value("isQueue", false))
                {
                    line << " [queue";
                    int rideId = p.value("rideId", -1);
                    if (rideId >= 0)
                    {
                        line << "→ride#" << rideId;
                    }
                    line << "]";
                }
                int edges = p.value("edges", 0);
                line << " connects:" << FormatEdges(edges);
            }
            if (element.contains("track"))
            {
                const auto& t = element["track"];
                line << " ride#" << t.value("rideId", -1);
            }
            if (element.contains("wall"))
            {
                const auto& w = element["wall"];
                int rotation = w.value("rotation", 0);
                // Convert rotation to direction string (per TileElementBase.h TILE_ELEMENT_DIRECTION_*)
                // 0=west, 1=north, 2=east, 3=south
                const char* directions[] = { "west", "north", "east", "south" };
                line << " dir:" << directions[rotation & 0x3];
            }
            canvas.Bullet(line.str());
        }
    }

    const auto& coverage = result.value("coverage", json::object());
    if (!coverage.empty())
    {
        canvas.Section("Coverage");
        canvas.KeyValue("Tiles modified", coverage.value("tilesModified", 0));
        int skipped = coverage.value("tilesSkipped", 0);
        if (skipped > 0)
        {
            canvas.KeyValue("Tiles skipped", skipped);
            const auto& examples = coverage.value("skippedExamples", json::array());
            if (!examples.empty())
            {
                std::ostringstream skippedList;
                for (size_t i = 0; i < examples.size(); ++i)
                {
                    if (i > 0)
                        skippedList << ", ";
                    skippedList << '(' << examples[i].value("x", 0) << ',' << examples[i].value("y", 0) << ')';
                }
                if (skipped > static_cast<int>(examples.size()))
                {
                    skippedList << " ...";
                }
                canvas.KeyValue("Skipped tiles", skippedList.str());
            }
            auto note = coverage.value("note", std::string(""));
            if (!note.empty())
            {
                canvas.Paragraph(note);
            }
        }
    }
}

void RenderMapArea(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Map Area");

    const auto& origin = result.value("origin", json::object());
    int originX = origin.value("x", 0);
    int originY = origin.value("y", 0);
    int width = result.value("width", 0);
    int height = result.value("height", 0);

    std::ostringstream anchor;
    anchor << "(" << originX << ", " << originY << ") top-left";
    canvas.KeyValue("Anchor", anchor.str());
    std::ostringstream span;
    span << width << "x" << height << " tiles";
    canvas.KeyValue("Span", span.str());

    const auto& rows = result.value("rows", json::array());
    if (!rows.empty())
    {
        std::cout << "    X:";
        for (int dx = 0; dx < width; ++dx)
        {
            std::cout << std::setw(2) << (originX + dx) << ' ';
        }
        std::cout << "\n";

        for (size_t i = 0; i < rows.size(); ++i)
        {
            int tileY = originY + static_cast<int>(i);
            std::string row = rows[i].get<std::string>();
            if (i == 0)
            {
                std::cout << "Y" << std::setw(3) << tileY << "  ";
            }
            else
            {
                std::cout << std::setw(4) << tileY << "  ";
            }
            for (char cell : row)
            {
                std::cout << ' ' << cell << ' ';
            }
            std::cout << "\n";
        }
    }
    else
    {
        canvas.Paragraph("No tiles to display.");
    }

    const auto& legend = result.value("legend", json::array());
    if (!legend.empty())
    {
        canvas.Section("Legend");
        for (const auto& entry : legend)
        {
            auto symbol = entry.value("symbol", std::string("?"));
            auto label = entry.value("label", std::string(""));
            canvas.Bullet(symbol + " = " + label);
        }
    }
}

void RenderMapHeatmap(const json& result)
{
    const auto& hotspots = result.value("hotspots", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Guest Hotspots");
    TableView table;
    table.headers = { "X", "Y", "Guests" };
    for (const auto& entry : hotspots)
    {
        table.rows.push_back({ std::to_string(entry.value("x", 0)), std::to_string(entry.value("y", 0)),
            std::to_string(entry.value("count", 0)) });
    }
    canvas.Table(table);
}

void RenderMapOwnership(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Ownership Bounds");

    auto formatBounds = [](const json& bounds) {
        if (bounds.is_null() || bounds.empty())
        {
            return std::string("None");
        }
        const auto& minCoords = bounds.value("min", json::object());
        const auto& maxCoords = bounds.value("max", json::object());
        int width = bounds.value("width", 0);
        int height = bounds.value("height", 0);
        uint64_t tileCount = bounds.value("tiles", 0ULL);
        std::ostringstream oss;
        oss << '(' << minCoords.value("x", 0) << ',' << minCoords.value("y", 0) << ") → ("
            << maxCoords.value("x", 0) << ',' << maxCoords.value("y", 0) << ')';
        if (width > 0 && height > 0)
        {
            oss << " • " << width << "x" << height;
        }
        if (tileCount > 0)
        {
            oss << " • " << tileCount << " tiles";
        }
        return oss.str();
    };

    int mapWidth = result.value("mapWidth", 0);
    int mapHeight = result.value("mapHeight", 0);
    if (mapWidth > 0 && mapHeight > 0)
    {
        std::ostringstream dims;
        dims << mapWidth << "x" << mapHeight << " tiles";
        canvas.KeyValue("Map", dims.str());
    }

    canvas.KeyValue("Owned land", formatBounds(result.value("owned", json::object())));
    canvas.KeyValue(
        "Construction rights", formatBounds(result.value("constructionRights", json::object())));
    canvas.KeyValue("For sale (land)", formatBounds(result.value("landForSale", json::object())));
    canvas.KeyValue(
        "For sale (rights)", formatBounds(result.value("constructionRightsForSale", json::object())));
}

void RenderEntrances(const json& result)
{
    const auto& entries = result.value("entrances", json::array());
    TextCanvas canvas(std::cout);
    canvas.Section("Entrances");
    canvas.KeyValue("Count", static_cast<int>(entries.size()));
    canvas.KeyValue("Park", result.value("parkOpen", false) ? "OPEN" : "CLOSED");

    TableView table;
    table.headers = { "Index", "X", "Y", "Z", "Facing" };
    for (const auto& e : entries)
    {
        table.rows.push_back({ std::to_string(e.value("index", 0)), std::to_string(e.value("x", 0)),
            std::to_string(e.value("y", 0)), std::to_string(e.value("z", 0)),
            e.value("direction", std::string("")) });
    }
    canvas.Table(table);
}

void RenderScan(const json& result)
{
    TextCanvas canvas(std::cout);

    std::string scanType = result.value("scanType", std::string("unknown"));
    if (scanType == "development")
    {
        canvas.Section("Development Scan");
    }
    else if (scanType == "guests")
    {
        canvas.Section("Guest Density Scan");
    }
    else
    {
        canvas.Section("Strategic Scan");
    }

    const auto& origin = result.value("origin", json::object());
    int originX = origin.value("x", 0);
    int originY = origin.value("y", 0);
    int zoom = result.value("zoom", 10);
    int gridSize = result.value("gridSize", 10);

    std::ostringstream anchor;
    anchor << "(" << originX << ", " << originY << ") top-left";
    canvas.KeyValue("Anchor", anchor.str());

    std::ostringstream zoomInfo;
    zoomInfo << zoom << "x (each cell = " << zoom << "×" << zoom << " tiles)";
    canvas.KeyValue("Zoom", zoomInfo.str());

    std::ostringstream coverage;
    int totalTiles = gridSize * zoom;
    coverage << totalTiles << "×" << totalTiles << " tiles (" << gridSize << "×" << gridSize << " cells)";
    canvas.KeyValue("Coverage", coverage.str());

    const auto& rows = result.value("rows", json::array());
    if (!rows.empty())
    {
        // Print column headers showing block coordinates
        std::cout << "    X:";
        for (int col = 0; col < gridSize; ++col)
        {
            int blockX = originX + (col * zoom);
            std::cout << std::setw(3) << blockX << ' ';
        }
        std::cout << "\n";

        // Print each row
        for (size_t i = 0; i < rows.size(); ++i)
        {
            int blockY = originY + (static_cast<int>(i) * zoom);
            std::string row = rows[i].get<std::string>();
            if (i == 0)
            {
                std::cout << "Y" << std::setw(3) << blockY << "  ";
            }
            else
            {
                std::cout << std::setw(4) << blockY << "  ";
            }
            for (char cell : row)
            {
                std::cout << ' ' << cell << "  ";
            }
            std::cout << "\n";
        }
    }
    else
    {
        canvas.Paragraph("No data to display.");
    }

    const auto& legend = result.value("legend", json::array());
    if (!legend.empty())
    {
        canvas.Section("Legend");
        for (const auto& entry : legend)
        {
            auto symbol = entry.value("symbol", std::string("?"));
            auto label = entry.value("label", std::string(""));
            canvas.Bullet(symbol + " = " + label);
        }
    }
}

void RenderSceneryClear(const json& result)
{
    TextCanvas canvas(std::cout);
    canvas.Section("Scenery Cleared");

    std::ostringstream area;
    area << "(" << result.value("x", 0) << ", " << result.value("y", 0) << ") anchor";
    int width = result.value("width", 1);
    int height = result.value("height", 1);
    if (width > 1 || height > 1)
    {
        area << " • " << width << "x" << height << " tiles";
    }
    canvas.KeyValue("Area", area.str());

    canvas.KeyValue("Cleared", result.value("cleared", std::string("scenery")));

    int64_t cost = result.value("cost", static_cast<int64_t>(0));
    if (cost != 0)
    {
        std::ostringstream costStr;
        costStr << "$" << (std::abs(cost) / 10) << "." << (std::abs(cost) % 10);
        if (cost < 0)
        {
            costStr << " refund";
        }
        canvas.KeyValue("Cost", costStr.str());
    }

    const auto& coverage = result.value("coverage", json::object());
    if (!coverage.empty())
    {
        canvas.KeyValue("Tiles processed", coverage.value("tilesProcessed", 0));
        int skipped = coverage.value("tilesSkipped", 0);
        if (skipped > 0)
        {
            canvas.KeyValue("Tiles skipped", skipped);
            const auto& examples = coverage.value("skippedExamples", json::array());
            if (!examples.empty())
            {
                std::ostringstream skippedList;
                for (size_t i = 0; i < examples.size(); ++i)
                {
                    if (i > 0)
                        skippedList << ", ";
                    skippedList << '(' << examples[i].value("x", 0) << ',' << examples[i].value("y", 0) << ')';
                }
                if (skipped > static_cast<int>(examples.size()))
                {
                    skippedList << " ...";
                }
                canvas.KeyValue("Skipped tiles", skippedList.str());
            }
            auto note = coverage.value("note", std::string(""));
            if (!note.empty())
            {
                canvas.Paragraph(note);
            }
        }
    }
}

} // namespace rctctl::renderers

#include "rctctl/commands/command_groups.hpp"

#include "rctctl/cli/cli.hpp"
#include "rctctl/renderers/map.hpp"
#include "rctctl/renderers/paths.hpp"
#include "rctctl/renderers/scenery.hpp"
#include "rctctl/renderers/trees.hpp"
#include "rctctl/renderers/walls.hpp"

#include <nlohmann/json.hpp>

namespace rctctl::commands {
namespace {
using json = nlohmann::json;

using cli::CommandArgSpec;
using cli::CommandPlan;
using cli::CommandSpec;
using cli::ParsedArgs;
}

void AppendEnvironmentCommands(std::vector<CommandSpec>& specs)
{
    specs.push_back(CommandSpec{
        "map",
        { "status" },
        "Show map summary.",
        "Displays map dimensions, ownership counts, and heights.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "map.status", json::object() };
        },
        renderers::RenderMapStatus });

    specs.push_back(CommandSpec{
        "map",
        { "tile" },
        "Inspect a tile.",
        "Shows ownership and elements at a tile (use --x, --y). "
        "For path elements, displays edge connectivity using isometric directions (NE/SE/SW/NW): "
        "NE=X-1, SE=Y+1, SW=X+1, NW=Y-1. Note: 'connects:' shows this path's edges only; "
        "guest pathfinding requires the adjacent tile to also have a path with a reciprocal edge. "
        "Also shows queue associations (which ride a queue path belongs to).",
        { CommandArgSpec{ "x", "Tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", true, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;
            return CommandPlan{ "map.tile", params };
        },
        renderers::RenderMapTile });

    specs.push_back(CommandSpec{
        "map",
        { "area" },
        "Render an ASCII mini-map.",
        "Prints a 16×16 tile grid anchored at --x/--y (top-left). If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner). Defaults to park center.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;
            return CommandPlan{ "map.area", params };
        },
        renderers::RenderMapArea });

    specs.push_back(CommandSpec{
        "map",
        { "area", "paths" },
        "Show footpaths and queues on ASCII mini-map.",
        "Displays footpaths (P), queues (Q), or empty (.) on 16×16 grid at --x/--y. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner). Defaults to park center.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;
            params["filter"] = "paths";
            return CommandPlan{ "map.area", params };
        },
        renderers::RenderMapArea });

    specs.push_back(CommandSpec{
        "map",
        { "area", "rides" },
        "Show rides and entrances on ASCII mini-map.",
        "Displays ride tracks (R), entrances (E), or empty (.) on 16×16 grid at --x/--y. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner). Defaults to park center.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;
            params["filter"] = "rides";
            return CommandPlan{ "map.area", params };
        },
        renderers::RenderMapArea });

    specs.push_back(CommandSpec{
        "map",
        { "area", "ownership" },
        "Show land ownership on ASCII mini-map.",
        "Displays owned (O), construction rights (c), or unowned (#) on 16×16 grid at --x/--y. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner). Defaults to park center.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;
            params["filter"] = "ownership";
            return CommandPlan{ "map.area", params };
        },
        renderers::RenderMapArea });

    specs.push_back(CommandSpec{
        "map",
        { "area", "scenery" },
        "Show trees and scenery on ASCII mini-map.",
        "Displays trees (T), scenery (S), or empty (.) on 16×16 grid at --x/--y. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner). Defaults to park center.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;
            params["filter"] = "scenery";
            return CommandPlan{ "map.area", params };
        },
        renderers::RenderMapArea });

    specs.push_back(CommandSpec{
        "map",
        { "area", "water" },
        "Show water on ASCII mini-map.",
        "Displays water (W) or land (.) on 16×16 grid at --x/--y. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner). Defaults to park center.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;
            params["filter"] = "water";
            return CommandPlan{ "map.area", params };
        },
        renderers::RenderMapArea });

    specs.push_back(CommandSpec{
        "map",
        { "area", "shops" },
        "Show shops and stalls on ASCII mini-map.",
        "Displays shops/stalls (S) or empty (.) on 16×16 grid at --x/--y. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner). Defaults to park center.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;
            params["filter"] = "shops";
            return CommandPlan{ "map.area", params };
        },
        renderers::RenderMapArea });

    specs.push_back(CommandSpec{
        "map",
        { "scan", "development" },
        "Strategic development density scan.",
        "Prints a 16×16 grid where each cell aggregates a block of game tiles. Use --zoom to control block size: 10 (10×10 tiles per cell) or 20 (20×20 tiles per cell). Displays infrastructure density: rides, paths, shops, and entrances. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner of scan area). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner of scan area). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "zoom", "Zoom level: 10 or 20 (default 10). Each grid cell represents ZOOM×ZOOM tiles.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;

            int zoom = 10;
            if (auto zoomParam = cli::GetIntOption(args, { "zoom" }))
            {
                zoom = *zoomParam;
                if (zoom != 10 && zoom != 20)
                {
                    throw std::runtime_error("Invalid: --zoom must be 10 or 20 (got " + std::to_string(zoom) + ")");
                }
            }
            params["zoom"] = zoom;
            return CommandPlan{ "scan.development", params };
        },
        renderers::RenderScan });

    specs.push_back(CommandSpec{
        "map",
        { "scan", "guests" },
        "Strategic guest density scan.",
        "Prints a 16×16 grid where each cell aggregates guest counts across a block of game tiles. Use --zoom to control block size: 10 (10×10 tiles per cell) or 20 (20×20 tiles per cell). Helps identify high-traffic park regions. If --x/--y are omitted, centers on owned park land.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner of scan area). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner of scan area). Defaults to park center.", false, "INT" },
          CommandArgSpec{ "zoom", "Zoom level: 10 or 20 (default 10). Each grid cell represents ZOOM×ZOOM tiles.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            auto xOpt = cli::GetIntOption(args, { "x" });
            auto yOpt = cli::GetIntOption(args, { "y" });

            if (xOpt && *xOpt < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(*xOpt) + ")");
            }
            if (yOpt && *yOpt < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(*yOpt) + ")");
            }

            if (xOpt)
                params["x"] = *xOpt;
            if (yOpt)
                params["y"] = *yOpt;

            int zoom = 10;
            if (auto zoomParam = cli::GetIntOption(args, { "zoom" }))
            {
                zoom = *zoomParam;
                if (zoom != 10 && zoom != 20)
                {
                    throw std::runtime_error("Invalid: --zoom must be 10 or 20 (got " + std::to_string(zoom) + ")");
                }
            }
            params["zoom"] = zoom;
            return CommandPlan{ "scan.guests", params };
        },
        renderers::RenderScan });

    specs.push_back(CommandSpec{
        "map",
        { "heatmap", "guests" },
        "Show guest hotspots.",
        "Lists tiles with highest guest density (optional --limit).",
        { CommandArgSpec{ "limit", "Hotspot count (default 10).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                if (*limit < 1)
                {
                    throw std::runtime_error("Invalid: --limit must be at least 1 (got " + std::to_string(*limit) + ")");
                }
                params["limit"] = *limit;
            }
            return CommandPlan{ "map.heatmapGuests", params };
        },
        renderers::RenderMapHeatmap });

    specs.push_back(CommandSpec{
        "map",
        { "ownership" },
        "Summarise land ownership bounds.",
        "Reports bounding boxes for owned land, construction rights, and parcels still for sale.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "map.ownership", json::object() };
        },
        renderers::RenderMapOwnership });

    specs.push_back(CommandSpec{
        "construction",
        { "land", "raise" },
        "Raise land by 2 tile units.",
        "Raises flat land at --x/--y (north-west corner) by one visual land step (2 tile units). Use --size to elevate every tile in an N×N square without slopes. Output shows the resulting tile state after the raise.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "size", "Square brush size in tiles (default 1).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;

            if (auto size = cli::GetIntOption(args, { "size" }))
            {
                if (*size < 1)
                {
                    throw std::runtime_error("Invalid: --size must be at least 1 (got " + std::to_string(*size) + ")");
                }
                params["width"] = *size;
                params["height"] = *size;
            }
            return CommandPlan{ "construction.landRaise", params };
        },
        renderers::RenderMapTile });

    specs.push_back(CommandSpec{
        "construction",
        { "land", "lower" },
        "Lower land by 2 tile units.",
        "Lowers flat land at --x/--y by one visual land step (2 tile units). Use --size to sink every tile in an N×N square with no slopes. Output shows the resulting tile state after the lower.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "size", "Square brush size in tiles (default 1).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;

            if (auto size = cli::GetIntOption(args, { "size" }))
            {
                if (*size < 1)
                {
                    throw std::runtime_error("Invalid: --size must be at least 1 (got " + std::to_string(*size) + ")");
                }
                params["width"] = *size;
                params["height"] = *size;
            }
            return CommandPlan{ "construction.landLower", params };
        },
        renderers::RenderMapTile });

    specs.push_back(CommandSpec{
        "construction",
        { "water", "raise" },
        "Raise water level.",
        "Raises water at --x/--y (north-west corner) by one step. Use --size to raise every tile in an N×N square.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "size", "Square brush size in tiles (default 1).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;

            if (auto size = cli::GetIntOption(args, { "size" }))
            {
                if (*size < 1)
                {
                    throw std::runtime_error("Invalid: --size must be at least 1 (got " + std::to_string(*size) + ")");
                }
                params["width"] = *size;
                params["height"] = *size;
            }
            return CommandPlan{ "construction.waterRaise", params };
        },
        renderers::RenderMapTile });

    specs.push_back(CommandSpec{
        "construction",
        { "water", "lower" },
        "Lower water level.",
        "Lowers water at --x/--y (north-west corner) by one step. Use --size to lower every tile in an N×N square.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "size", "Square brush size in tiles (default 1).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;

            if (auto size = cli::GetIntOption(args, { "size" }))
            {
                if (*size < 1)
                {
                    throw std::runtime_error("Invalid: --size must be at least 1 (got " + std::to_string(*size) + ")");
                }
                params["width"] = *size;
                params["height"] = *size;
            }
            return CommandPlan{ "construction.waterLower", params };
        },
        renderers::RenderMapTile });

    specs.push_back(CommandSpec{
        "construction",
        { "scenery", "clear" },
        "Clear scenery from an area.",
        "Clears scenery from an N×N area anchored at --x/--y (north-west corner). At least one filter flag "
        "is required: --small (trees, flowers, statues, fences), --large (multi-tile structures), or --paths "
        "(footpaths). Multiple flags can be combined. Use --size for larger brush.",
        { CommandArgSpec{ "x", "Tile X coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate (north-west corner).", true, "INT" },
          CommandArgSpec{ "size", "Square brush size in tiles (default 1).", false, "INT" },
          CommandArgSpec{ "small", "Clear small scenery and walls.", false, "BOOL" },
          CommandArgSpec{ "large", "Clear large scenery.", false, "BOOL" },
          CommandArgSpec{ "paths", "Clear footpaths.", false, "BOOL" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;

            if (auto size = cli::GetIntOption(args, { "size" }))
            {
                if (*size < 1)
                {
                    throw std::runtime_error("Invalid: --size must be at least 1 (got " + std::to_string(*size) + ")");
                }
                params["width"] = *size;
                params["height"] = *size;
            }

            bool small = cli::GetBoolOption(args, { "small" }).value_or(false);
            bool large = cli::GetBoolOption(args, { "large" }).value_or(false);
            bool paths = cli::GetBoolOption(args, { "paths" }).value_or(false);

            if (!small && !large && !paths)
            {
                throw std::runtime_error(
                    "At least one filter flag is required: --small (trees, scenery, walls), --large (multi-tile structures), or --paths (footpaths)");
            }

            params["small"] = small;
            params["large"] = large;
            params["paths"] = paths;

            return CommandPlan{ "construction.sceneryClear", params };
        },
        renderers::RenderSceneryClear });

    specs.push_back(CommandSpec{
        "trees",
        { "catalog" },
        "List available tree objects.",
        "Shows all loaded tree scenery objects with identifiers, prices, and heights. Use the identifier with --tree-id in 'trees place'.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "trees.catalog", json::object() };
        },
        renderers::RenderTreeCatalog });

    specs.push_back(CommandSpec{
        "trees",
        { "place" },
        "Place a scenery tree.",
        "Places the requested tree object at the given tile coordinate (optionally specify height). Use 'trees catalog' to find available tree identifiers.",
        { CommandArgSpec{ "x", "Tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", true, "INT" },
          CommandArgSpec{ "tree-id", "Object identifier for the tree scenery item.", true, "STRING" },
          CommandArgSpec{ "z", "Optional height in tile units.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x tile coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y tile coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;
            params["tree"] = cli::RequireStringOption(args, { "tree-id", "tree" }, "tree identifier");

            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                if (*z < 0)
                {
                    throw std::runtime_error("Invalid: --z must be non-negative (got " + std::to_string(*z) + ")");
                }
                params["z"] = *z;
            }
            return CommandPlan{ "trees.place", params };
        },
        renderers::RenderTreePlant });

    specs.push_back(CommandSpec{
        "trees",
        { "remove" },
        "Remove trees (see: construction scenery clear).",
        "To remove trees and small scenery, use 'construction scenery clear --small'. "
        "Example: rctctl construction scenery clear --x 50 --y 50 --small --size 3",
        { CommandArgSpec{ "x", "Tile X coordinate.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", false, "INT" } },
        [](const ParsedArgs&) -> CommandPlan {
            throw std::runtime_error(
                "Tree removal uses the construction scenery clear tool.\n"
                "\n"
                "Usage: rctctl construction scenery clear --x <X> --y <Y> --small [--size N]\n"
                "\n"
                "The --small flag clears trees, flowers, statues, and walls.\n"
                "\n"
                "Examples:\n"
                "  Remove single tile:    construction scenery clear --x 50 --y 50 --small\n"
                "  Remove 3x3 area:       construction scenery clear --x 50 --y 50 --small --size 3");
        },
        nullptr });

    specs.push_back(CommandSpec{
        "scenery",
        { "catalog" },
        "List available scenery items.",
        "Shows all loaded small scenery objects (excluding trees) with identifiers, prices, and flags. Use the identifier with --scenery-id in 'scenery place'.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "scenery.catalog", json::object() };
        },
        renderers::RenderSceneryCatalog });

    specs.push_back(CommandSpec{
        "scenery",
        { "place" },
        "Place a scenery item.",
        "Places the requested scenery object at the given tile coordinate (optionally specify height, quadrant, facing, and colors). Use 'scenery catalog' to find available items.",
        { CommandArgSpec{ "x", "Tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", true, "INT" },
          CommandArgSpec{ "scenery-id", "Object identifier for the scenery item (e.g. rct2.scenery_small.lamp1).", true, "STRING" },
          CommandArgSpec{ "z", "Optional height in tile units.", false, "INT" },
          CommandArgSpec{ "quadrant", "Tile quadrant 0-3 for placement.", false, "INT" },
          CommandArgSpec{ "facing", "Direction: north|south|east|west (for rotatable scenery).", false, "STRING" },
          CommandArgSpec{ "primary-colour", "Primary colour index (0-31).", false, "INT" },
          CommandArgSpec{ "secondary-colour", "Secondary colour index (0-31).", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x tile coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y tile coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;
            params["scenery"] = cli::RequireStringOption(args, { "scenery-id", "scenery" }, "scenery identifier");

            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                if (*z < 0)
                {
                    throw std::runtime_error("Invalid: --z must be non-negative (got " + std::to_string(*z) + ")");
                }
                params["z"] = *z;
            }
            if (auto quad = cli::GetIntOption(args, { "quadrant" }))
            {
                if (*quad < 0 || *quad > 3)
                {
                    throw std::runtime_error("Invalid: --quadrant must be 0-3 (got " + std::to_string(*quad) + ")");
                }
                params["quadrant"] = *quad;
            }
            if (auto facing = cli::GetStringOption(args, { "facing" }))
            {
                params["facing"] = *facing;
            }
            if (auto colour = cli::GetIntOption(args, { "primary-colour" }))
            {
                if (*colour < 0 || *colour > 31)
                {
                    throw std::runtime_error("Invalid: --primary-colour must be 0-31 (got " + std::to_string(*colour) + ")");
                }
                params["primaryColour"] = *colour;
            }
            if (auto colour = cli::GetIntOption(args, { "secondary-colour" }))
            {
                if (*colour < 0 || *colour > 31)
                {
                    throw std::runtime_error("Invalid: --secondary-colour must be 0-31 (got " + std::to_string(*colour) + ")");
                }
                params["secondaryColour"] = *colour;
            }
            return CommandPlan{ "scenery.place", params };
        },
        renderers::RenderSceneryPlace });

    specs.push_back(CommandSpec{
        "scenery",
        { "remove" },
        "Remove scenery (see: construction scenery clear).",
        "To remove small scenery items, use 'construction scenery clear --small'. "
        "For large multi-tile scenery, use '--large'. "
        "Example: rctctl construction scenery clear --x 50 --y 50 --small --size 3",
        { CommandArgSpec{ "x", "Tile X coordinate.", false, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", false, "INT" } },
        [](const ParsedArgs&) -> CommandPlan {
            throw std::runtime_error(
                "Scenery removal uses the construction scenery clear tool.\n"
                "\n"
                "Usage: rctctl construction scenery clear --x <X> --y <Y> [--small] [--large] [--size N]\n"
                "\n"
                "Flags:\n"
                "  --small   Clear small scenery (flowers, statues, fences, trees)\n"
                "  --large   Clear large multi-tile scenery structures\n"
                "\n"
                "Examples:\n"
                "  Remove small scenery:   construction scenery clear --x 50 --y 50 --small\n"
                "  Remove large scenery:   construction scenery clear --x 50 --y 50 --large\n"
                "  Remove both in 3x3:     construction scenery clear --x 50 --y 50 --small --large --size 3");
        },
        nullptr });

    specs.push_back(CommandSpec{
        "path-items",
        { "catalog" },
        "List path items (benches, bins, lamps).",
        "Shows available path additions that can be placed on footpaths. Use --category to filter: benches, bins, lamps, fountains. Supports aliases like 'bench', 'bin', 'lamp' in place commands.",
        { CommandArgSpec{ "category", "Filter by category: benches, bins, lamps, fountains.", false, "STRING" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto cat = cli::GetStringOption(args, { "category" }))
            {
                params["category"] = *cat;
            }
            return CommandPlan{ "path-items.catalog", params };
        },
        renderers::RenderPathItemsCatalog });

    specs.push_back(CommandSpec{
        "path-items",
        { "place" },
        "Place a path item (bench, bin, lamp).",
        "Adds a path item to an existing footpath tile. Use friendly names like 'bench', 'bin', 'lamp', or full identifiers from 'path-items catalog'.",
        { CommandArgSpec{ "x", "Tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", true, "INT" },
          CommandArgSpec{ "item-id", "Item identifier or alias (e.g. 'bench', 'bin', 'lamp', or rct2.footpath_item.bench1).", true, "STRING" },
          CommandArgSpec{ "z", "Optional height in tile units. Only needed if multiple paths at different heights.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x tile coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y tile coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;
            params["item"] = cli::RequireStringOption(args, { "item-id", "item" }, "item identifier");

            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                if (*z < 0)
                {
                    throw std::runtime_error("Invalid: --z must be non-negative (got " + std::to_string(*z) + ")");
                }
                params["z"] = *z;
            }
            return CommandPlan{ "path-items.place", params };
        },
        renderers::RenderPathItemsPlace });

    specs.push_back(CommandSpec{
        "path-items",
        { "remove" },
        "Remove a path item.",
        "Removes the path item (bench, bin, lamp, etc.) from a footpath tile.",
        { CommandArgSpec{ "x", "Tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", true, "INT" },
          CommandArgSpec{ "z", "Optional height in tile units. Only needed if multiple paths at different heights.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x tile coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y tile coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;

            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                if (*z < 0)
                {
                    throw std::runtime_error("Invalid: --z must be non-negative (got " + std::to_string(*z) + ")");
                }
                params["z"] = *z;
            }
            return CommandPlan{ "path-items.remove", params };
        },
        renderers::RenderPathItemsRemove });

    specs.push_back(CommandSpec{
        "path-items",
        { "list" },
        "List path items (benches, bins, lamps) placed on footpaths.",
        "Lists items placed ON footpaths (benches, bins, lamps, fountains, queue screens) - NOT the footpaths themselves. "
        "For footpath layout, use 'map tile' or 'map area paths'. "
        "Shows type, coordinates, broken/vandalized status, and bin fullness. "
        "Use --type to filter: bench, bin, lamp, fountain, queue_screen, or all (default). "
        "Use --broken to show only vandalized items that need repair. Supports pagination via --after cursor.",
        { CommandArgSpec{ "limit", "Max items to return (default 50).", false, "INT" },
          CommandArgSpec{ "after", "Resume after this cursor ID for pagination.", false, "STRING" },
          CommandArgSpec{ "type", "Filter by type: bench, bin, lamp, fountain, queue_screen, all.", false, "TYPE" },
          CommandArgSpec{ "broken", "Only show broken/vandalized items.", false, "BOOL" },
          CommandArgSpec{ "order", "Sort by: type, broken, x, y (default: type).", false, "FIELD" },
          CommandArgSpec{ "direction", "Sort order: asc, desc (default: asc).", false, "DIR" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            if (auto limit = cli::GetIntOption(args, { "limit" }))
            {
                if (*limit < 1)
                {
                    throw std::runtime_error("Invalid: --limit must be at least 1 (got " + std::to_string(*limit) + ")");
                }
                params["limit"] = *limit;
            }
            if (auto after = cli::GetStringOption(args, { "after" }))
            {
                params["after"] = *after;
            }
            if (auto type = cli::GetStringOption(args, { "type" }))
            {
                std::string lower = *type;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower != "bench" && lower != "bin" && lower != "lamp" && lower != "fountain"
                    && lower != "queue_screen" && lower != "all")
                {
                    throw std::runtime_error("Invalid: --type must be bench, bin, lamp, fountain, queue_screen, or all");
                }
                params["type"] = lower;
            }
            if (auto broken = cli::GetBoolOption(args, { "broken" }))
            {
                params["broken"] = *broken;
            }
            if (auto order = cli::GetStringOption(args, { "order" }))
            {
                std::string lower = *order;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower != "type" && lower != "broken" && lower != "x" && lower != "y")
                {
                    throw std::runtime_error("Invalid: --order must be type, broken, x, or y");
                }
                params["order"] = lower;
            }
            if (auto dir = cli::GetStringOption(args, { "direction" }))
            {
                std::string lower = *dir;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower != "asc" && lower != "desc")
                {
                    throw std::runtime_error("Invalid: --direction must be asc or desc");
                }
                params["direction"] = lower;
            }
            return CommandPlan{ "paths.list", params };
        },
        renderers::RenderPathsList });

    specs.push_back(CommandSpec{
        "paths",
        { "place" },
        "Place a footpath tile.",
        "Places a footpath at --x/--y. "
        "For regular paths, use surface names: tarmac, dirt, crazy, ash. "
        "For QUEUE paths, use queue surfaces: queue_blue, queue_red, queue_yellow, queue_green. "
        "Without --z, places on terrain with auto-detected slope (or at water height if the tile is water). "
        "With --z, places an elevated path at explicit height (flat unless --slope specified).",
        { CommandArgSpec{ "x", "Tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", true, "INT" },
          CommandArgSpec{ "surface", "Surface type. Regular: tarmac, dirt, crazy, ash. Queue: queue_blue, queue_red, queue_yellow, queue_green.", true, "STRING" },
          CommandArgSpec{ "railings", "Railings style (wood, concrete, space, bamboo). Auto-selected for queue paths if omitted.", false, "STRING" },
          CommandArgSpec{ "z", "Explicit height in tile units for elevated paths (ground level is typically 14). Omit for ground paths.", false, "INT" },
          CommandArgSpec{ "slope", "Slope direction for elevated ramps: north, south, east, west. Requires --z.", false, "STRING" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x tile coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y tile coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;
            params["surface"] = cli::RequireStringOption(args, { "surface", "surface-id" }, "surface identifier");

            if (auto rail = cli::GetStringOption(args, { "railings", "railings-id" }))
            {
                params["railings"] = *rail;
            }

            // Elevated path parameters
            auto zOpt = cli::GetIntOption(args, { "z" });
            auto slopeOpt = cli::GetStringOption(args, { "slope" });

            // Validate: --slope requires --z
            if (slopeOpt && !zOpt)
            {
                throw std::runtime_error("Invalid: --slope requires --z for elevated path placement");
            }

            // Validate slope direction if provided
            if (slopeOpt)
            {
                std::string lower = *slopeOpt;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower != "north" && lower != "south" && lower != "east" && lower != "west" && lower != "n"
                    && lower != "s" && lower != "e" && lower != "w")
                {
                    throw std::runtime_error("Invalid: --slope must be north, south, east, or west");
                }
                params["slope"] = *slopeOpt;
            }

            if (zOpt)
            {
                params["z"] = *zOpt;
            }

            return CommandPlan{ "paths.place", params };
        },
        renderers::RenderPathPlace });

    specs.push_back(CommandSpec{
        "paths",
        { "catalog" },
        "List available footpath surfaces and railings.",
        "Enumerates all loaded path surface and railing objects with identifiers for use with 'paths place'.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "paths.catalog", json::object() };
        },
        renderers::RenderPathCatalog });

    specs.push_back(CommandSpec{
        "paths",
        { "remove" },
        "Remove a single footpath tile.",
        "Removes the footpath at --x/--y coordinates. "
        "Use --z to specify height when multiple paths exist at different levels. "
        "For bulk removal, use 'construction scenery clear --x X --y Y --paths --size N'.",
        { CommandArgSpec{ "x", "Tile X coordinate.", true, "INT" },
          CommandArgSpec{ "y", "Tile Y coordinate.", true, "INT" },
          CommandArgSpec{ "z", "Height in tile units. Only needed if multiple paths at different heights.", false, "INT" } },
        [](const ParsedArgs& args) {
            json params = json::object();
            int x = cli::RequireIntOption(args, { "x" }, "x tile coordinate");
            int y = cli::RequireIntOption(args, { "y" }, "y tile coordinate");

            if (x < 0)
            {
                throw std::runtime_error("Invalid: --x must be non-negative (got " + std::to_string(x) + ")");
            }
            if (y < 0)
            {
                throw std::runtime_error("Invalid: --y must be non-negative (got " + std::to_string(y) + ")");
            }

            params["x"] = x;
            params["y"] = y;

            if (auto z = cli::GetIntOption(args, { "z" }))
            {
                if (*z < 0)
                {
                    throw std::runtime_error("Invalid: --z must be non-negative (got " + std::to_string(*z) + ")");
                }
                params["z"] = *z;
            }
            return CommandPlan{ "paths.remove", params };
        },
        renderers::RenderPathRemove });

    specs.push_back(CommandSpec{
        "entrances",
        { "list" },
        "List park entrances.",
        "Displays park entrance coordinates and state.",
        {},
        [](const ParsedArgs&) {
            return CommandPlan{ "infrastructure.entrances", json::object() };
        },
        renderers::RenderEntrances });
}

} // namespace rctctl::commands

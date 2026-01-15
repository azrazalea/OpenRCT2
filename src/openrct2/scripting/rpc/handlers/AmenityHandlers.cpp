/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef ENABLE_SCRIPTING

#include "../HandlerRegistry.h"
#include "HandlerInit.h"
#include "../RpcTypes.h"
#include "../RpcUtils.h"

#include "../../../Context.h"
#include "../../../GameState.h"
#include "../../../actions/FootpathAdditionPlaceAction.h"
#include "../../../actions/GameActionResult.h"
#include "../../../actions/RideCreateAction.h"
#include "../../../actions/RideDemolishAction.h"
#include "../../../actions/TrackPlaceAction.h"
#include "../../../core/Money.hpp"
#include "../../../entity/EntityList.h"
#include "../../../localisation/Formatting.h"
#include "../../../object/ObjectManager.h"
#include "../../../object/ObjectEntryManager.h"
#include "../../../object/PathAdditionEntry.h"
#include "../../../object/PathAdditionObject.h"
#include "../../../ride/Ride.h"
#include "../../../ride/RideData.h"
#include "../../../ride/RideManager.hpp"
#include "../../../world/Footpath.h"
#include "../../../world/Location.hpp"
#include "../../../world/Map.h"
#include "../../../world/TileElementsView.h"
#include "../../../world/tile_element/PathElement.h"
#include "../../../world/tile_element/EntranceElement.h"
#include "../../../world/tile_element/SurfaceElement.h"
#include "../../../world/tile_element/SmallSceneryElement.h"
#include "../../../world/tile_element/LargeSceneryElement.h"
#include "../../../world/tile_element/WallElement.h"
#include "../../../world/tile_element/TrackElement.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;

    namespace
    {
        // =========================================================================
        // Constants
        // =========================================================================

        static const std::string kBenchIdentifier = "rct2.footpath_item.bench1";
        static const std::string kBinIdentifier = "rct2.footpath_item.litter1";

        // Cache TTL in seconds
        static constexpr int kCacheTTLSeconds = 120;

        // Default fallback costs (used only if object lookup fails)
        static constexpr int kDefaultBenchCost = 50;
        static constexpr int kDefaultBinCost = 50;
        static constexpr int kDefaultBathroomCost = 2000;
        static constexpr int kDefaultFirstAidCost = 3000;

        // Get actual price for a path addition from game objects
        money64 GetPathAdditionPrice(const std::string& identifier)
        {
            auto* context = GetContext();
            if (context == nullptr)
                return 0;

            auto& objManager = context->GetObjectManager();
            auto entryIndex = objManager.GetLoadedObjectEntryIndex(identifier);
            if (entryIndex == kObjectEntryIndexNull)
                return 0;

            auto* entry = OpenRCT2::ObjectManager::GetObjectEntry<PathAdditionEntry>(entryIndex);
            if (entry == nullptr)
                return 0;

            return entry->price;
        }

        // Get actual build cost for a ride type
        money64 GetRideBuildCost(ride_type_t rideType)
        {
            const auto& rtd = GetRideTypeDescriptor(rideType);
            return rtd.BuildCosts.TrackPrice;
        }

        // Direction offsets for adjacent tile checks (in world coords)
        static const std::array<CoordsXY, 4> kOrthogonalOffsets = {
            CoordsXY{ 0, -32 },  // North
            CoordsXY{ 32, 0 },   // East
            CoordsXY{ 0, 32 },   // South
            CoordsXY{ -32, 0 }   // West
        };

        // TileDirectionDelta from Map.cpp - the standard direction mapping
        // Used for entrances/exits, path edges, everything
        // Direction: 0=West, 1=South, 2=East, 3=North, 4=SW, 5=SE, 6=NE, 7=NW
        static constexpr int kTileDirDeltaX[] = { -1, 0, 1, 0, -1, 1, 1, -1 };
        static constexpr int kTileDirDeltaY[] = { 0, 1, 0, -1, 1, 1, -1, -1 };

        // Check if a path at the given location is connected in the specified direction
        // This mimics MapCoordIsConnected from Map.cpp
        // faceDirection: the direction we need the path to have an edge pointing to
        bool IsPathConnected(int tileX, int tileY, int targetZ, uint8_t faceDirection, TileCoordsXYZ& outPath)
        {
            CoordsXY worldCoords{ tileX * 32, tileY * 32 };

            if (!MapIsLocationValid(worldCoords))
                return false;

            for (auto* pathElement : TileElementsView<PathElement>(worldCoords))
            {
                int pathZ = pathElement->BaseHeight;  // BaseHeight is in 8ths

                if (pathElement->IsSloped())
                {
                    uint8_t slopeDir = static_cast<uint8_t>(pathElement->GetSlopeDirection());
                    // Slope going up in direction faceDirection
                    if (slopeDir == faceDirection && targetZ == pathZ + 2)
                    {
                        outPath = TileCoordsXYZ{ tileX, tileY, pathZ };
                        return true;
                    }
                    // Slope going down (reverse direction)
                    uint8_t reverseDir = (faceDirection + 2) & 3;
                    if (slopeDir == reverseDir && targetZ == pathZ)
                    {
                        outPath = TileCoordsXYZ{ tileX, tileY, pathZ };
                        return true;
                    }
                }
                else
                {
                    // Flat path: check if height matches AND has edge in required direction
                    if (targetZ == pathZ && (pathElement->GetEdges() & (1 << faceDirection)))
                    {
                        outPath = TileCoordsXYZ{ tileX, tileY, pathZ };
                        return true;
                    }
                }
            }
            return false;
        }

        // Find the connected path for an entrance/exit using the game's algorithm
        // exitX, exitY, exitZ: the entrance/exit tile position
        // exitDir: the direction the entrance/exit faces (0-7)
        // Returns true if a connected path is found
        bool FindConnectedPath(int exitX, int exitY, int exitZ, uint8_t exitDir, TileCoordsXYZ& outPath)
        {
            // Move OPPOSITE to exit direction to find the path tile
            // (subtract the delta, which is how the game does it)
            int pathX = exitX - kTileDirDeltaX[exitDir & 7];
            int pathY = exitY - kTileDirDeltaY[exitDir & 7];

            // The path needs to have an edge pointing in the exit's direction
            return IsPathConnected(pathX, pathY, exitZ, exitDir & 3, outPath);
        }

        // =========================================================================
        // Data Structures
        // =========================================================================

        struct PathTileAnalysis
        {
            TileCoordsXYZ location;
            bool isJunction = false;
            bool isQueue = false;
            bool isSloped = false;
            bool isWide = false;
            int edgeCount = 0;       // Number of connected edges (0-4)
            int freeEdgeCount = 0;   // Number of free edges = 4 - edgeCount (items appear on free edges)
            bool hasAddition = false;
            bool isBroken = false;   // If true, addition was vandalized
            std::string additionType;
            bool canPlaceBench = true;    // Benches blocked by slopes
            bool canPlaceBin = true;      // Bins CAN go on slopes
            std::string blockReason;
            int existingBenchCount = 0;   // Actual bench count = freeEdgeCount when has bench
            int existingBinCount = 0;     // Actual bin count = freeEdgeCount when has bin
            int existingLampCount = 0;
            int existingOtherCount = 0;
            bool isBuildingBlocked = false;
            bool isFull = false;
        };

        struct RideExitInfo
        {
            RideId rideId;
            std::string rideName;
            TileCoordsXYZ exitLocation;
            TileCoordsXYZ pathLocation;  // Adjacent path tile (where guests exit onto)
            double nauseaRating = 0.0;
        };

        struct FoodShopInfo
        {
            RideId rideId;
            std::string shopName;
            TileCoordsXYZ location;      // Shop's tile position
            TileCoordsXYZ pathLocation;  // Adjacent path tile (where guests access shop)
        };

        struct AmenityHeatValue
        {
            TileCoordsXYZ location;
            double benchValue = 0.0;
            double binValue = 0.0;
            double bathroomValue = 0.0;
            double firstAidValue = 0.0;
            int freeEdgeCount = 0;       // Number of items this path can hold (1-3 typically)
            bool canPlaceBench = true;   // Benches can't go on slopes
            bool canPlaceBin = true;     // Bins CAN go on slopes
            std::string blockReason;
            std::string primaryReason;  // Why this location scores well
        };

        struct BuildingCandidate
        {
            TileCoordsXYZ location;
            Direction direction;
            double score;
            TileCoordsXYZ adjacentPath;
            std::string reason;
        };

        // Placement plan for a single amenity
        struct PlannedPlacement
        {
            std::string type;  // "bench", "bin", "bathroom", "firstaid"
            TileCoordsXYZ location;
            double score;
            std::string reason;
            int estimatedCost;
        };

        // Full recommendation plan
        struct RecommendationPlan
        {
            std::string id;
            std::vector<PlannedPlacement> placements;
            int totalCost;
            int budget;
            std::string summary;
            std::chrono::steady_clock::time_point createdAt;
        };

        // =========================================================================
        // Analysis Cache
        // =========================================================================

        struct ParkAnalysisCache
        {
            std::vector<PathTileAnalysis> paths;
            std::vector<RideExitInfo> nauseousExits;
            std::vector<FoodShopInfo> foodShops;
            std::vector<AmenityHeatValue> heatMap;
            std::vector<BuildingCandidate> buildingCandidates;
            std::chrono::steady_clock::time_point timestamp;
            bool valid = false;

            // Park fingerprint to detect when a different park is loaded
            std::string cachedParkName;

            bool IsExpired() const
            {
                if (!valid) return true;

                // Check if park changed by comparing park name
                auto& gameState = getGameState();
                if (gameState.park.name != cachedParkName)
                    return true;

                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count();
                return elapsed > kCacheTTLSeconds;
            }

            void Invalidate()
            {
                valid = false;
            }
        };

        static ParkAnalysisCache g_analysisCache;
        static std::unordered_map<std::string, RecommendationPlan> g_recommendationPlans;
        static std::mutex g_cacheMutex;
        static int g_nextPlanId = 1;

        // Plan expiration time (5 minutes)
        static constexpr int kPlanTTLSeconds = 300;

        // Clean up old recommendation plans (called with mutex held)
        void CleanupOldPlans()
        {
            auto now = std::chrono::steady_clock::now();
            std::vector<std::string> toRemove;

            for (const auto& pair : g_recommendationPlans)
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - pair.second.createdAt).count();
                if (elapsed > kPlanTTLSeconds)
                {
                    toRemove.push_back(pair.first);
                }
            }

            for (const auto& id : toRemove)
            {
                g_recommendationPlans.erase(id);
            }
        }

        // =========================================================================
        // Helper Functions
        // =========================================================================

        int CountEdges(uint8_t edges)
        {
            int count = 0;
            for (int i = 0; i < 4; i++)
            {
                if (edges & (1 << i))
                    count++;
            }
            return count;
        }

        bool IsJunction(const PathElement* pathElement)
        {
            uint8_t edges = pathElement->GetEdges();
            return CountEdges(edges) >= 3;
        }

        bool HasBlockingAdjacentElements(const CoordsXY& coords, int32_t baseZ)
        {
            for (const auto& offset : kOrthogonalOffsets)
            {
                CoordsXY neighborCoords = coords + offset;
                if (!MapIsLocationValid(neighborCoords))
                    continue;

                for (auto* element : TileElementsView<WallElement>(neighborCoords))
                {
                    if (std::abs(element->GetBaseZ() - baseZ) <= 16)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        uint64_t MakeTileKey(const TileCoordsXYZ& t)
        {
            // Pack x, y, z into a 64-bit key
            // x: bits 42-63 (22 bits), y: bits 21-41 (21 bits), z: bits 0-20 (21 bits)
            uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(t.x)) & 0x3FFFFF;
            uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(t.y)) & 0x1FFFFF;
            uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(t.z)) & 0x1FFFFF;
            return (ux << 42) | (uy << 21) | uz;
        }

        // Minimum score thresholds - stop placing when quality drops too low
        constexpr double kMinBenchScore = 1.0;
        constexpr double kMinBinScore = 0.8;

        // =========================================================================
        // Analysis Functions
        // =========================================================================

        std::vector<PathTileAnalysis> AnalyzeAllPaths()
        {
            std::vector<PathTileAnalysis> paths;

            auto& gameState = getGameState();
            for (int32_t y = 0; y < gameState.mapSize.y; y++)
            {
                for (int32_t x = 0; x < gameState.mapSize.x; x++)
                {
                    CoordsXY worldCoords{ x * 32, y * 32 };
                    if (!MapIsLocationValid(worldCoords))
                        continue;

                    for (auto* pathElement : TileElementsView<PathElement>(worldCoords))
                    {
                        PathTileAnalysis analysis;
                        analysis.location = TileCoordsXYZ{ x, y, pathElement->GetBaseZ() / 8 };
                        analysis.isJunction = IsJunction(pathElement);
                        analysis.isQueue = pathElement->IsQueue();
                        analysis.isSloped = pathElement->IsSloped();
                        analysis.isWide = pathElement->IsWide();
                        analysis.edgeCount = CountEdges(pathElement->GetEdges());
                        analysis.freeEdgeCount = 4 - analysis.edgeCount;  // Items appear on free edges
                        analysis.hasAddition = pathElement->HasAddition();
                        analysis.isBroken = pathElement->IsBroken();

                        if (analysis.hasAddition)
                        {
                            auto* additionEntry = pathElement->GetAdditionEntry();
                            if (additionEntry != nullptr)
                            {
                                // Item count = free edges (items appear on unconnected edges)
                                int itemCount = analysis.freeEdgeCount;
                                if (additionEntry->flags & PATH_ADDITION_FLAG_IS_BENCH)
                                {
                                    analysis.additionType = "bench";
                                    analysis.existingBenchCount = itemCount;
                                }
                                else if (additionEntry->flags & PATH_ADDITION_FLAG_IS_BIN)
                                {
                                    analysis.additionType = "bin";
                                    analysis.existingBinCount = itemCount;
                                }
                                else if (additionEntry->flags & PATH_ADDITION_FLAG_LAMP)
                                {
                                    analysis.additionType = "lamp";
                                    analysis.existingLampCount = itemCount;
                                }
                                else
                                {
                                    analysis.additionType = "other";
                                    analysis.existingOtherCount = itemCount;
                                }
                            }
                            analysis.isFull = true;  // Path already has an addition
                        }

                        // Determine what can be placed on this tile
                        // Check land ownership first - can't place on land not owned by park
                        CoordsXYZ ownershipCheckCoords{ worldCoords.x, worldCoords.y, pathElement->GetBaseZ() };
                        if (!MapIsLocationOwned(ownershipCheckCoords))
                        {
                            analysis.canPlaceBench = false;
                            analysis.canPlaceBin = false;
                            analysis.blockReason = "land not owned";
                        }
                        // No free edges = can't place anything (4-way junction)
                        else if (analysis.freeEdgeCount == 0)
                        {
                            analysis.canPlaceBench = false;
                            analysis.canPlaceBin = false;
                            analysis.blockReason = "no free edges";
                        }
                        // Queue paths block everything
                        else if (analysis.isQueue)
                        {
                            analysis.canPlaceBench = false;
                            analysis.canPlaceBin = false;
                            analysis.blockReason = "queue line";
                        }
                        else if (analysis.hasAddition)
                        {
                            // Already has an addition - check if it's a junction/building-blocked
                            // which only allows 1, or a regular path which might allow 2
                            bool isRestricted = analysis.isJunction ||
                                HasBlockingAdjacentElements(worldCoords, pathElement->GetBaseZ());
                            if (isRestricted)
                            {
                                analysis.canPlaceBench = false;
                                analysis.canPlaceBin = false;
                                analysis.blockReason = "junction/building (max 1 addition)";
                            }
                            else
                            {
                                // Regular path with 1 addition - might allow another
                                // but for simplicity, block if any addition exists
                                analysis.canPlaceBench = false;
                                analysis.canPlaceBin = false;
                                analysis.blockReason = "existing addition";
                            }
                        }
                        else if (analysis.isJunction)
                        {
                            // Junction without addition - allow placement (1 allowed)
                            analysis.blockReason = "";
                            // But slopes still block benches even on junctions
                            if (analysis.isSloped)
                            {
                                analysis.canPlaceBench = false;
                                analysis.blockReason = "sloped junction (bins ok)";
                            }
                        }
                        else if (HasBlockingAdjacentElements(worldCoords, pathElement->GetBaseZ()))
                        {
                            // Building-adjacent without addition - allow placement (1 allowed)
                            analysis.isBuildingBlocked = true;
                            analysis.blockReason = "";
                            // But slopes still block benches even on building-adjacent
                            if (analysis.isSloped)
                            {
                                analysis.canPlaceBench = false;
                                analysis.blockReason = "sloped path near building (bins ok)";
                            }
                        }
                        else if (analysis.isSloped)
                        {
                            // Slopes block benches but NOT bins
                            analysis.canPlaceBench = false;
                            // canPlaceBin stays true
                            analysis.blockReason = "sloped path (bins ok)";
                        }

                        paths.push_back(analysis);
                    }
                }
            }
            return paths;
        }

        std::vector<RideExitInfo> GetNauseousRideExits()
        {
            std::vector<RideExitInfo> exits;

            for (const auto& ride : RideManager(getGameState()))
            {
                auto classification = ride.getClassification();
                if (classification != RideClassification::ride)
                    continue;

                if (ride.ratings.isNull())
                    continue;

                // RCT2 nausea ratings are stored as value * 100 (e.g., 490 = 4.90 displayed)
                // Use the display value directly - higher nausea = more benches needed
                double nausea = static_cast<double>(ride.ratings.nausea) / 100.0;

                if (nausea < 0.5)
                    continue;  // Skip rides with minimal nausea (< 0.50)

                for (StationIndex::UnderlyingType i = 0; i < ride.numStations; i++)
                {
                    const auto& station = ride.getStation(StationIndex::FromUnderlying(i));
                    if (!station.Exit.IsNull())
                    {
                        RideExitInfo info;
                        info.rideId = ride.id;
                        info.rideName = ride.getName();
                        info.exitLocation = TileCoordsXYZ{
                            station.Exit.x,
                            station.Exit.y,
                            station.Exit.z
                        };
                        info.nauseaRating = nausea;

                        // Find the connected path tile using the game's connectivity algorithm
                        // This matches MapCoordIsConnected() and RideEntranceExitIsReachable()
                        uint8_t exitDir = static_cast<uint8_t>(station.Exit.direction);
                        TileCoordsXYZ pathLoc;
                        bool foundPath = FindConnectedPath(
                            station.Exit.x, station.Exit.y, station.Exit.z,
                            exitDir, pathLoc);

                        if (foundPath)
                        {
                            exits.push_back(info);
                        }
                    }
                }
            }
            return exits;
        }

        std::vector<FoodShopInfo> GetFoodShops()
        {
            std::vector<FoodShopInfo> shops;

            for (const auto& ride : RideManager(getGameState()))
            {
                const auto& rtd = ride.getRideTypeDescriptor();
                if (!rtd.HasFlag(RtdFlag::sellsFood) && !rtd.HasFlag(RtdFlag::sellsDrinks))
                    continue;

                if (ride.numStations > 0)
                {
                    const auto& station = ride.getStation(StationIndex::FromUnderlying(0));

                    // Validate station location before using
                    int tileX = station.Start.x / 32;
                    int tileY = station.Start.y / 32;
                    if (tileX <= 0 || tileY <= 0)
                        continue;  // Invalid location

                    auto mapSize = GetMapSizeMinus2();
                    if (tileX >= mapSize.x / 32 || tileY >= mapSize.y / 32)
                        continue;  // Out of bounds

                    FoodShopInfo info;
                    info.rideId = ride.id;
                    info.shopName = ride.getName();

                    // Find the track element to get the shop's facing direction and Z
                    CoordsXY worldCoords{ tileX * 32, tileY * 32 };
                    Direction shopDirection = Direction(0);  // Default south
                    int shopZ = 0;

                    for (auto* trackElement : TileElementsView<TrackElement>(worldCoords))
                    {
                        if (trackElement->GetRideIndex() == ride.id)
                        {
                            shopDirection = trackElement->GetDirection();
                            shopZ = trackElement->GetBaseZ() / 8;
                            break;
                        }
                    }

                    info.location = TileCoordsXYZ{ tileX, tileY, shopZ };

                    // Find connected path using the game's connectivity algorithm
                    uint8_t dirIdx = static_cast<uint8_t>(shopDirection);
                    TileCoordsXYZ pathLoc;
                    bool hasPath = FindConnectedPath(tileX, tileY, shopZ, dirIdx, pathLoc);

                    if (hasPath)
                    {
                        info.pathLocation = pathLoc;
                    }

                    if (hasPath)
                    {
                        shops.push_back(info);
                    }
                }
            }
            return shops;
        }

        std::unordered_map<uint64_t, int> ComputePathDistances(const TileCoordsXYZ& start, int maxDist = 30)
        {
            std::unordered_map<uint64_t, int> distances;
            std::queue<std::pair<TileCoordsXYZ, int>> queue;

            queue.push({ start, 0 });
            distances[MakeTileKey(start)] = 0;

            while (!queue.empty())
            {
                auto [current, dist] = queue.front();
                queue.pop();

                if (dist >= maxDist)
                    continue;  // Early termination for efficiency

                CoordsXY worldCoords{ current.x * 32, current.y * 32 };

                // Find path at current location with matching Z
                PathElement* currentPath = nullptr;
                for (auto* pathElement : TileElementsView<PathElement>(worldCoords))
                {
                    if (pathElement->GetBaseZ() / 8 == current.z)
                    {
                        currentPath = pathElement;
                        break;
                    }
                }

                if (currentPath == nullptr)
                    continue;

                uint8_t edges = currentPath->GetEdges();
                bool currentSloped = currentPath->IsSloped();
                int currentSlopeDir = currentSloped ? static_cast<int>(currentPath->GetSlopeDirection()) : -1;

                // Direction mapping: 0=West(-x), 1=South(+y), 2=East(+x), 3=North(-y)
                const int dx[] = { -1, 0, 1, 0 };
                const int dy[] = { 0, 1, 0, -1 };

                for (int dir = 0; dir < 4; dir++)
                {
                    if (!(edges & (1 << dir)))
                        continue;

                    int neighborX = current.x + dx[dir];
                    int neighborY = current.y + dy[dir];
                    CoordsXY neighborWorld{ neighborX * 32, neighborY * 32 };

                    if (!MapIsLocationValid(neighborWorld))
                        continue;

                    // Calculate expected Z based on slope
                    // If current path is sloped and we're leaving in slope direction, we go UP (+2 Z)
                    int expectedZ = current.z;
                    if (currentSloped && currentSlopeDir == dir)
                        expectedZ = current.z + 2;  // Going up the slope

                    // Find path at neighbor location
                    for (auto* neighborPath : TileElementsView<PathElement>(neighborWorld))
                    {
                        int neighborZ = neighborPath->GetBaseZ() / 8;

                        // Check if this path connects back to us (has edge in opposite direction)
                        uint8_t neighborEdges = neighborPath->GetEdges();
                        int oppositeDir = (dir + 2) % 4;
                        if (!(neighborEdges & (1 << oppositeDir)))
                            continue;

                        // Check Z connectivity with slope logic
                        bool validZ = false;

                        if (neighborZ == expectedZ)
                        {
                            // Direct connection at expected Z
                            validZ = true;
                        }
                        else if (neighborPath->IsSloped())
                        {
                            // Neighbor is sloped - check if we can connect going down into it
                            int neighborSlopeDir = static_cast<int>(neighborPath->GetSlopeDirection());
                            // If neighbor slopes towards us (opposite dir), we enter at its high end (Z+2)
                            if (neighborSlopeDir == oppositeDir && neighborZ + 2 == expectedZ)
                                validZ = true;
                        }

                        if (!validZ)
                            continue;

                        TileCoordsXYZ neighbor{ neighborX, neighborY, neighborZ };
                        uint64_t key = MakeTileKey(neighbor);

                        if (distances.find(key) == distances.end())
                        {
                            distances[key] = dist + 1;
                            queue.push({ neighbor, dist + 1 });
                        }
                    }
                }
            }

            return distances;
        }

        // Shared spreading penalty function - reduces scores of nearby tiles after placement
        // Uses path distance (BFS) for accurate penalty application
        // freeEdgeCount: number of items placed (tiles with more free edges hold more benches/bins)
        void ApplySpreadingPenalty(std::vector<AmenityHeatValue>& candidates, const TileCoordsXYZ& placed, bool isBench, int freeEdgeCount)
        {
            auto pathDistances = ComputePathDistances(placed, 10);
            // Scale penalty by item count - 3 benches on one tile = stronger penalty
            double itemScale = std::max(1, freeEdgeCount) / 3.0;

            for (auto& c : candidates)
            {
                uint64_t key = MakeTileKey(c.location);
                auto it = pathDistances.find(key);
                if (it == pathDistances.end())
                    continue;

                int pathDist = it->second;

                if (pathDist < 10)
                {
                    double falloff = (10.0 - pathDist) / 10.0;
                    // Benches: 1.5 penalty, bins: 2.5 penalty (stronger to reduce count)
                    double basePenalty = isBench ? 1.5 : 2.5;
                    double reduction = basePenalty * itemScale * falloff * falloff;

                    if (isBench)
                        c.benchValue = std::max(0.0, c.benchValue - reduction);
                    else
                        c.binValue = std::max(0.0, c.binValue - reduction);
                }
            }
        }

        std::vector<AmenityHeatValue> CalculateAmenityHeatMap(
            const std::vector<PathTileAnalysis>& paths,
            const std::vector<RideExitInfo>& nauseousExits,
            const std::vector<FoodShopInfo>& foodShops)
        {
            std::vector<AmenityHeatValue> heatMap;

            // Precompute path distances from all food shops
            // Use pathLocation (the adjacent path tile) not location (the shop tile)
            std::vector<std::unordered_map<uint64_t, int>> foodDistances;
            for (const auto& shop : foodShops)
            {
                foodDistances.push_back(ComputePathDistances(shop.pathLocation));
            }

            // Precompute path distances from all nauseous ride exits
            std::vector<std::unordered_map<uint64_t, int>> exitDistances;
            for (const auto& exit : nauseousExits)
            {
                exitDistances.push_back(ComputePathDistances(exit.pathLocation));
            }

            // Find existing benches and bins (store as XYZ for path distance lookup)
            std::vector<TileCoordsXYZ> existingBenches;
            std::vector<TileCoordsXYZ> existingBins;
            for (const auto& path : paths)
            {
                if (path.existingBenchCount > 0)
                    existingBenches.push_back(path.location);
                if (path.existingBinCount > 0)
                    existingBins.push_back(path.location);
            }

            // Precompute path distances from existing benches and bins
            std::vector<std::unordered_map<uint64_t, int>> benchDistances;
            for (const auto& bench : existingBenches)
            {
                benchDistances.push_back(ComputePathDistances(bench, 15));  // Smaller range for spreading
            }
            std::vector<std::unordered_map<uint64_t, int>> binDistances;
            for (const auto& bin : existingBins)
            {
                binDistances.push_back(ComputePathDistances(bin, 15));
            }

            for (const auto& path : paths)
            {
                AmenityHeatValue heat;
                heat.location = path.location;
                heat.freeEdgeCount = path.freeEdgeCount;
                heat.canPlaceBench = path.canPlaceBench;
                heat.canPlaceBin = path.canPlaceBin;
                heat.blockReason = path.blockReason;

                // Base value for general coverage
                // Benches have higher base - guests always appreciate seating
                // Bins have very low base - only placed near food/high traffic without POI bonus
                heat.benchValue = 0.5;
                heat.binValue = 0.05;

                uint64_t tileKey = MakeTileKey(path.location);
                std::string bestReason;
                double bestReasonScore = 0.0;

                // Factor 1: Path distance from same object type (further = better)
                int minBenchPathDist = 1000;
                for (const auto& benchDist : benchDistances)
                {
                    auto it = benchDist.find(tileKey);
                    if (it != benchDist.end())
                    {
                        minBenchPathDist = std::min(minBenchPathDist, it->second);
                    }
                }
                heat.benchValue += std::min(1.0, static_cast<double>(minBenchPathDist) / 10.0);

                int minBinPathDist = 1000;
                for (const auto& binDist : binDistances)
                {
                    auto it = binDist.find(tileKey);
                    if (it != binDist.end())
                    {
                        minBinPathDist = std::min(minBinPathDist, it->second);
                    }
                }
                heat.binValue += std::min(1.0, static_cast<double>(minBinPathDist) / 10.0);

                // Factor 2: Path distance from nauseous ride exits
                // Only BENCHES help with nausea - guests seek benches to sit when sick
                // High weight to create CLUSTERS of benches near nauseous exits
                bool nearExit = false;
                for (size_t i = 0; i < nauseousExits.size(); i++)
                {
                    auto it = exitDistances[i].find(tileKey);
                    if (it == exitDistances[i].end())
                        continue;

                    int pathDist = it->second;
                    const auto& exit = nauseousExits[i];

                    // Benches: 0-12 path tiles from exit - guests may need to sit immediately
                    if (pathDist <= 12)
                    {
                        nearExit = true;
                        // nauseaRating scaled by 0.7, quadratic falloff favors closer tiles
                        // dist 0 gets full bonus, dist 12 gets nothing
                        double falloff = (12.0 - pathDist) / 12.0;
                        double benchContrib = exit.nauseaRating * 0.7 * falloff * falloff;
                        heat.benchValue += benchContrib;
                        if (benchContrib > bestReasonScore)
                        {
                            bestReasonScore = benchContrib;
                            bestReason = "near " + exit.rideName + " exit";
                        }
                    }

                    // Bins do NOT help with nausea - removed bin scoring from exits

                    // Bathroom/FirstAid value (for future use)
                    if (pathDist < 20)
                    {
                        double facilityContrib = exit.nauseaRating * std::max(0.0, 1.0 - pathDist / 20.0);
                        heat.bathroomValue += facilityContrib;
                        heat.firstAidValue += facilityContrib * 1.5;
                    }
                }

                // Factor 3: Path distance from food shops
                // Benches get bonus near food (guests sit to eat)
                // Bins penalized close (guests still eating), spreading penalty handles distribution
                bool nearFood = false;
                for (size_t i = 0; i < foodShops.size(); i++)
                {
                    auto it = foodDistances[i].find(tileKey);
                    if (it != foodDistances[i].end())
                    {
                        int pathDist = it->second;

                        // Simple quadratic bonus for being near food shops
                        if (pathDist <= 15)
                        {
                            nearFood = true;
                            double falloff = (15.0 - pathDist) / 15.0;
                            double bonus = falloff * falloff;

                            // Benches always get bonus
                            heat.benchValue += 1.5 * bonus;

                            // Bins get bonus but not right at shop (pathDist <= 1)
                            if (pathDist > 1)
                                heat.binValue += 0.6 * bonus;

                            if (bestReason.empty())
                                bestReason = "near " + foodShops[i].shopName;
                        }
                    }
                }

                // Penalize bins near ride exits but NOT near food
                // Bins don't help nauseous guests, only benches do
                if (nearExit && !nearFood)
                {
                    heat.binValue *= 0.1;  // Heavy penalty - bins shouldn't go near exits
                }

                heat.primaryReason = bestReason;
                heatMap.push_back(heat);
            }

            return heatMap;
        }

        std::vector<BuildingCandidate> FindBuildingCandidates(
            const std::vector<PathTileAnalysis>& paths,
            const std::vector<RideExitInfo>& nauseousExits,
            const std::vector<FoodShopInfo>& foodShops)
        {
            std::vector<BuildingCandidate> candidates;
            std::unordered_set<uint64_t> checkedTiles;

            // Precompute path distances from exits and food shops for scoring
            std::vector<std::unordered_map<uint64_t, int>> exitDistances;
            for (const auto& exit : nauseousExits)
            {
                exitDistances.push_back(ComputePathDistances(exit.pathLocation, 25));
            }
            std::vector<std::unordered_map<uint64_t, int>> foodDistances;
            for (const auto& shop : foodShops)
            {
                foodDistances.push_back(ComputePathDistances(shop.pathLocation, 20));
            }

            // Direction offsets for building placement (orthogonal only)
            // kTileDirDeltaX/Y: 0=West, 1=South, 2=East, 3=North

            for (const auto& path : paths)
            {
                for (int d = 0; d < 4; d++)
                {
                    TileCoordsXYZ candidate{
                        path.location.x + kTileDirDeltaX[d],
                        path.location.y + kTileDirDeltaY[d],
                        path.location.z  // Use adjacent path's Z
                    };
                    Direction facing = static_cast<Direction>(d);

                    uint64_t key = MakeTileKey(candidate);
                    if (checkedTiles.count(key) > 0)
                        continue;
                    checkedTiles.insert(key);

                    CoordsXY worldCoords{ candidate.x * 32, candidate.y * 32 };
                    if (!MapIsLocationValid(worldCoords))
                        continue;

                    bool canPlace = true;

                    for (auto* element : TileElementsView<TileElement>(worldCoords))
                    {
                        if (element == nullptr)
                            break;

                        auto type = element->GetType();
                        if (type == TileElementType::Path ||
                            type == TileElementType::Track ||
                            type == TileElementType::LargeScenery ||
                            type == TileElementType::Entrance ||
                            type == TileElementType::SmallScenery)
                        {
                            canPlace = false;
                            break;
                        }

                        if (element->IsLastForTile())
                            break;
                    }

                    if (!canPlace)
                        continue;

                    double score = 0.0;
                    std::string reason;

                    // Use path distance from adjacent path tile for scoring
                    uint64_t pathKey = MakeTileKey(path.location);

                    for (size_t i = 0; i < nauseousExits.size(); i++)
                    {
                        auto it = exitDistances[i].find(pathKey);
                        if (it != exitDistances[i].end())
                        {
                            int pathDist = it->second;
                            if (pathDist < 20)
                            {
                                double contrib = nauseousExits[i].nauseaRating * (20.0 - pathDist) / 20.0;
                                score += contrib;
                                if (contrib > 0.3 && reason.empty())
                                {
                                    reason = "near " + nauseousExits[i].rideName;
                                }
                            }
                        }
                    }

                    for (size_t i = 0; i < foodShops.size(); i++)
                    {
                        auto it = foodDistances[i].find(pathKey);
                        if (it != foodDistances[i].end())
                        {
                            int pathDist = it->second;
                            if (pathDist < 15)
                            {
                                score += (15.0 - pathDist) / 15.0;
                            }
                        }
                    }

                    candidates.push_back({
                        candidate,
                        facing,
                        score,
                        path.location,
                        reason
                    });
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                [](const BuildingCandidate& a, const BuildingCandidate& b) {
                    return a.score > b.score;
                });

            return candidates;
        }

        // =========================================================================
        // Cache Management
        // =========================================================================

        void RefreshCacheIfNeeded()
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);

            if (!g_analysisCache.IsExpired())
                return;

            g_analysisCache.paths = AnalyzeAllPaths();
            g_analysisCache.nauseousExits = GetNauseousRideExits();
            g_analysisCache.foodShops = GetFoodShops();
            g_analysisCache.heatMap = CalculateAmenityHeatMap(
                g_analysisCache.paths,
                g_analysisCache.nauseousExits,
                g_analysisCache.foodShops);
            g_analysisCache.buildingCandidates = FindBuildingCandidates(
                g_analysisCache.paths,
                g_analysisCache.nauseousExits,
                g_analysisCache.foodShops);
            g_analysisCache.timestamp = std::chrono::steady_clock::now();
            g_analysisCache.cachedParkName = getGameState().park.name;
            g_analysisCache.valid = true;
        }

        // Returns a reference to the cached analysis. Safe to use within a single handler
        // since all RPC handlers execute on the game thread and are not concurrent.
        // The reference remains valid for the duration of the handler call.
        // Note: If multi-threaded access becomes needed, this should return a shared_ptr
        // to an immutable copy instead.
        const ParkAnalysisCache& GetCachedAnalysis()
        {
            RefreshCacheIfNeeded();
            return g_analysisCache;
        }

        // =========================================================================
        // RPC Handlers
        // =========================================================================

        RpcResult HandleAmenitiesAnalyze(const json_t& params)
        {
            auto* context = GetContext();
            if (context == nullptr)
                return RpcResult::Error(kErrorInvalidParams, "Game context not available");

            const auto& cache = GetCachedAnalysis();

            // Build path statistics
            int existingBenches = 0, existingBins = 0, existingLamps = 0, existingOther = 0;
            int placeableCount = 0, junctionCount = 0, queueCount = 0;
            int buildingBlockedCount = 0, fullTileCount = 0;

            for (const auto& path : cache.paths)
            {
                existingBenches += path.existingBenchCount;
                existingBins += path.existingBinCount;
                existingLamps += path.existingLampCount;
                existingOther += path.existingOtherCount;

                if (path.canPlaceBench || path.canPlaceBin)
                    placeableCount++;
                if (path.isJunction)
                    junctionCount++;
                if (path.isQueue)
                    queueCount++;
                if (path.isBuildingBlocked)
                    buildingBlockedCount++;
                if (path.isFull)
                    fullTileCount++;
            }

            json_t payload = json_t::object();

            // Path statistics (matching renderer expectations)
            json_t pathStats = json_t::object();
            pathStats["totalPaths"] = static_cast<int>(cache.paths.size());
            pathStats["placeableTiles"] = placeableCount;
            pathStats["junctions"] = junctionCount;
            pathStats["queueLines"] = queueCount;
            pathStats["buildingBlocked"] = buildingBlockedCount;
            pathStats["fullTiles"] = fullTileCount;
            payload["pathStats"] = pathStats;

            // Existing amenities (matching renderer expectations)
            json_t existingAmenities = json_t::object();
            existingAmenities["benches"] = existingBenches;
            existingAmenities["bins"] = existingBins;
            existingAmenities["lamps"] = existingLamps;
            existingAmenities["other"] = existingOther;
            payload["existingAmenities"] = existingAmenities;

            // Nauseous exits (matching renderer expectations)
            json_t nauseousExits = json_t::array();
            for (const auto& exit : cache.nauseousExits)
            {
                json_t r = json_t::object();
                r["rideName"] = exit.rideName;
                r["nauseaRating"] = exit.nauseaRating;  // Actual nausea value (e.g., 4.90)
                r["x"] = exit.exitLocation.x;
                r["y"] = exit.exitLocation.y;
                nauseousExits.push_back(r);
            }
            payload["nauseousExits"] = nauseousExits;

            // Food shops (matching renderer expectations)
            json_t foodShops = json_t::array();
            for (const auto& shop : cache.foodShops)
            {
                json_t s = json_t::object();
                s["shopName"] = shop.shopName;
                s["x"] = shop.location.x;
                s["y"] = shop.location.y;
                // Also include the path location where BFS starts (for debugging)
                s["pathX"] = shop.pathLocation.x;
                s["pathY"] = shop.pathLocation.y;
                foodShops.push_back(s);
            }
            payload["foodShops"] = foodShops;

            // Top candidates (combined bench/bin scores for renderer)
            std::vector<AmenityHeatValue> sorted;
            for (const auto& h : cache.heatMap)
            {
                if (h.canPlaceBench || h.canPlaceBin)
                    sorted.push_back(h);
            }
            // Sort by combined value
            std::sort(sorted.begin(), sorted.end(),
                [](const AmenityHeatValue& a, const AmenityHeatValue& b) {
                    return (a.benchValue + a.binValue) > (b.benchValue + b.binValue);
                });

            json_t topCandidates = json_t::array();
            for (size_t i = 0; i < std::min(size_t(10), sorted.size()); i++)
            {
                const auto& t = sorted[i];
                json_t rec = json_t::object();
                rec["x"] = t.location.x;
                rec["y"] = t.location.y;
                rec["z"] = t.location.z;
                rec["benchValue"] = t.benchValue;
                rec["binValue"] = t.binValue;
                if (!t.primaryReason.empty())
                    rec["reason"] = t.primaryReason;
                topCandidates.push_back(rec);
            }
            payload["topCandidates"] = topCandidates;

            // Additional info for Claude agents
            json_t meta = json_t::object();
            meta["cacheAgeSeconds"] = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - cache.timestamp).count();
            meta["cacheTTLSeconds"] = kCacheTTLSeconds;
            payload["meta"] = meta;

            return RpcResult::Ok(payload);
        }

        RpcResult HandleAmenitiesRecommend(const json_t& params)
        {
            auto budgetParam = GetIntParam(params, "budget");
            if (!budgetParam)
                return RpcResult::Error(kErrorInvalidParams, "budget parameter is required");

            int budget = *budgetParam;
            if (budget <= 0)
                return RpcResult::Error(kErrorInvalidParams, "budget must be positive");

            auto* context = GetContext();
            if (context == nullptr)
                return RpcResult::Error(kErrorInvalidParams, "Game context not available");

            const auto& cache = GetCachedAnalysis();

            // Get actual prices from game objects (money64 is stored as 10x, e.g. £30 = 300)
            money64 benchCost = GetPathAdditionPrice(kBenchIdentifier);
            if (benchCost <= 0) benchCost = ToMoney64FromGBP(kDefaultBenchCost);
            money64 binCost = GetPathAdditionPrice(kBinIdentifier);
            if (binCost <= 0) binCost = ToMoney64FromGBP(kDefaultBinCost);
            money64 bathroomCost = GetRideBuildCost(RIDE_TYPE_TOILETS);
            if (bathroomCost <= 0) bathroomCost = ToMoney64FromGBP(kDefaultBathroomCost);

            // Convert user's budget to money64 format (multiply by 10)
            money64 budgetMoney = ToMoney64FromGBP(budget);

            // Parse priority (default: benches, bins, bathrooms)
            std::vector<std::string> priorities = { "benches", "bins", "bathrooms" };
            std::vector<std::string> unknownPriorities;
            static const std::unordered_set<std::string> validPriorities = { "benches", "bins", "bathrooms" };

            if (params.contains("priority") && params["priority"].is_array())
            {
                priorities.clear();
                for (const auto& p : params["priority"])
                {
                    if (p.is_string())
                    {
                        std::string pstr = p.get<std::string>();
                        if (validPriorities.count(pstr) > 0)
                        {
                            priorities.push_back(pstr);
                        }
                        else
                        {
                            unknownPriorities.push_back(pstr);
                        }
                    }
                }
                // Fall back to defaults if all priorities were invalid
                if (priorities.empty())
                {
                    priorities = { "benches", "bins", "bathrooms" };
                }
            }

            // Parse max counts
            int maxBenches = 50, maxBins = 50, maxBathrooms = 5;
            if (params.contains("maxBenches"))
                maxBenches = params["maxBenches"].get<int>();
            if (params.contains("maxBins"))
                maxBins = params["maxBins"].get<int>();
            if (params.contains("maxBathrooms"))
                maxBathrooms = params["maxBathrooms"].get<int>();

            // Build placement plan
            RecommendationPlan plan;
            plan.budget = budget;
            plan.totalCost = 0;
            plan.createdAt = std::chrono::steady_clock::now();

            // Track what we've used
            std::unordered_set<uint64_t> usedTiles;
            int benchCount = 0, binCount = 0, bathroomCount = 0;

            // Build separate candidate lists based on what can actually be placed
            std::vector<AmenityHeatValue> benchCandidates, binCandidates;
            for (const auto& h : cache.heatMap)
            {
                if (h.canPlaceBench)
                    benchCandidates.push_back(h);
                if (h.canPlaceBin)
                    binCandidates.push_back(h);
            }

            // ============ CLUSTER SEEDING PASS ============
            // Ensure minimum coverage per zone before optimizing within zones
            // Each nauseous exit gets at least 1 bench
            // Each food shop gets at least 1 bench (close) + 1 bin (further out)

            struct Zone {
                TileCoordsXYZ center;
                std::string name;
                bool isFoodShop;
                int benchesNeeded;  // Number of benches to seed in this zone
                int binsNeeded;     // Number of bins to seed in this zone
            };

            // Clustering helper - groups nearby points together using path distance
            auto clusterPoints = [](const std::vector<TileCoordsXYZ>& points,
                                    const std::vector<std::string>& names,
                                    int clusterRadius) -> std::vector<std::tuple<TileCoordsXYZ, std::string, int>> {
                std::vector<bool> clustered(points.size(), false);
                std::vector<std::tuple<TileCoordsXYZ, std::string, int>> clusters;  // center, name, count

                for (size_t i = 0; i < points.size(); i++)
                {
                    if (clustered[i])
                        continue;

                    // Compute path distances from this point
                    auto pathDists = ComputePathDistances(points[i], clusterRadius + 1);

                    // Start a new cluster with this point
                    std::vector<size_t> clusterMembers = { i };
                    clustered[i] = true;

                    // Find all nearby unclustered points using path distance
                    for (size_t j = i + 1; j < points.size(); j++)
                    {
                        if (clustered[j])
                            continue;

                        uint64_t key = MakeTileKey(points[j]);
                        auto it = pathDists.find(key);
                        if (it != pathDists.end() && it->second <= clusterRadius)
                        {
                            clusterMembers.push_back(j);
                            clustered[j] = true;
                        }
                    }

                    // Use first point as cluster center (it's on the path network)
                    TileCoordsXYZ center = points[clusterMembers[0]];

                    // Build cluster name
                    std::string clusterName;
                    if (clusterMembers.size() == 1)
                    {
                        clusterName = names[clusterMembers[0]];
                    }
                    else
                    {
                        clusterName = names[clusterMembers[0]] + " area";
                    }

                    clusters.emplace_back(center, clusterName, static_cast<int>(clusterMembers.size()));
                }
                return clusters;
            };

            std::vector<Zone> zones;

            // Cluster nauseous ride exits (within 8 path tiles = same area)
            {
                std::vector<TileCoordsXYZ> exitPoints;
                std::vector<std::string> exitNames;
                for (const auto& exit : cache.nauseousExits)
                {
                    if (exit.nauseaRating >= 1.0)
                    {
                        exitPoints.push_back(exit.pathLocation);
                        exitNames.push_back(exit.rideName);
                    }
                }

                auto exitClusters = clusterPoints(exitPoints, exitNames, 8);
                for (const auto& [center, name, count] : exitClusters)
                {
                    Zone z;
                    z.center = center;
                    z.name = name;
                    z.isFoodShop = false;
                    z.benchesNeeded = count;  // One bench per ride exit in cluster
                    z.binsNeeded = 0;
                    zones.push_back(z);
                }
            }

            // Cluster food shops (within 8 path tiles = food court)
            {
                std::vector<TileCoordsXYZ> shopPoints;
                std::vector<std::string> shopNames;
                for (const auto& shop : cache.foodShops)
                {
                    shopPoints.push_back(shop.pathLocation);
                    shopNames.push_back(shop.shopName);
                }

                auto shopClusters = clusterPoints(shopPoints, shopNames, 8);
                for (const auto& [center, name, count] : shopClusters)
                {
                    Zone z;
                    z.center = center;
                    z.name = (count > 1) ? "Food Court" : name;
                    z.isFoodShop = true;
                    z.benchesNeeded = count;  // One bench per shop
                    z.binsNeeded = count;     // One bin per shop, spread around cluster
                    zones.push_back(z);
                }
            }

            // Helper to find best candidate near a zone center using path distance
            auto findBestNearZone = [](const std::vector<AmenityHeatValue>& candidates,
                                       const TileCoordsXYZ& center, int maxDist,
                                       const std::unordered_set<uint64_t>& usedTiles,
                                       bool useBenchValue, int minDist = 0) -> size_t {
                // Compute path distances from zone center
                auto pathDists = ComputePathDistances(center, maxDist + 1);

                size_t bestIdx = SIZE_MAX;
                double bestScore = 0;

                for (size_t i = 0; i < candidates.size(); i++)
                {
                    uint64_t key = MakeTileKey(candidates[i].location);
                    if (usedTiles.count(key) > 0)
                        continue;

                    auto it = pathDists.find(key);
                    if (it == pathDists.end())
                        continue;  // Not reachable by path

                    int pathDist = it->second;
                    if (pathDist < minDist || pathDist > maxDist)
                        continue;

                    double score = useBenchValue ? candidates[i].benchValue : candidates[i].binValue;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestIdx = i;
                    }
                }
                return bestIdx;
            };

            // Seed each zone with minimum coverage
            // Place items one at a time, spreading across zones, until all zones are seeded
            bool madeProgress = true;
            while (madeProgress)
            {
                madeProgress = false;

                for (auto& zone : zones)
                {
                    // Place one bench if needed (within 12 tiles of zone center)
                    if (zone.benchesNeeded > 0 && benchCount < maxBenches && plan.totalCost + benchCost <= budgetMoney)
                    {
                        size_t idx = findBestNearZone(benchCandidates, zone.center, 12, usedTiles, true, 0);

                        if (idx != SIZE_MAX)
                        {
                            const auto& c = benchCandidates[idx];
                            uint64_t key = MakeTileKey(c.location);

                            PlannedPlacement p;
                            p.type = "bench";
                            p.location = c.location;
                            p.score = c.benchValue;
                            p.reason = "seeding " + zone.name;
                            p.estimatedCost = static_cast<int>(benchCost / 10);

                            plan.placements.push_back(p);
                            plan.totalCost += benchCost;
                            usedTiles.insert(key);
                            benchCount++;
                            zone.benchesNeeded--;
                            madeProgress = true;

                            ApplySpreadingPenalty(benchCandidates, c.location, true, c.freeEdgeCount);
                        }
                    }

                    // Place one bin if needed (food shops only)
                    // No minDist - scoring penalizes pathDist 0-2 so bins naturally go to pathDist 3+
                    if (zone.binsNeeded > 0 && binCount < maxBins && plan.totalCost + binCost <= budgetMoney)
                    {
                        size_t idx = findBestNearZone(binCandidates, zone.center, 15, usedTiles, false, 0);

                        if (idx != SIZE_MAX)
                        {
                            const auto& c = binCandidates[idx];
                            uint64_t key = MakeTileKey(c.location);

                            PlannedPlacement p;
                            p.type = "bin";
                            p.location = c.location;
                            p.score = c.binValue;
                            p.reason = "seeding " + zone.name;
                            p.estimatedCost = static_cast<int>(binCost / 10);

                            plan.placements.push_back(p);
                            plan.totalCost += binCost;
                            usedTiles.insert(key);
                            binCount++;
                            zone.binsNeeded--;
                            madeProgress = true;

                            ApplySpreadingPenalty(binCandidates, c.location, false, c.freeEdgeCount);
                        }
                    }
                }
            }

            // ============ MAIN PLACEMENT LOOP ============
            // Interleaved placement: alternate between benches and bins for balance
            // Priority affects which we start with and slight preference, not exclusive ordering
            bool preferBenches = (priorities.empty() || priorities[0] == "benches");
            bool placeBenchNext = preferBenches;

            while (plan.totalCost < budgetMoney &&
                   (benchCount < maxBenches || binCount < maxBins || bathroomCount < maxBathrooms))
            {
                // Find best candidate for each type
                double bestBenchScore = -1, bestBinScore = -1;
                size_t bestBenchIdx = SIZE_MAX, bestBinIdx = SIZE_MAX;

                for (size_t i = 0; i < benchCandidates.size(); i++)
                {
                    uint64_t key = MakeTileKey(benchCandidates[i].location);
                    if (usedTiles.count(key) == 0 && benchCandidates[i].benchValue > bestBenchScore)
                    {
                        bestBenchScore = benchCandidates[i].benchValue;
                        bestBenchIdx = i;
                    }
                }

                for (size_t i = 0; i < binCandidates.size(); i++)
                {
                    uint64_t key = MakeTileKey(binCandidates[i].location);
                    if (usedTiles.count(key) == 0 && binCandidates[i].binValue > bestBinScore)
                    {
                        bestBinScore = binCandidates[i].binValue;
                        bestBinIdx = i;
                    }
                }

                // Determine what to place - interleave benches and bins
                // Also enforce minimum score thresholds to avoid mediocre placements
                std::string chosenType;
                bool canPlaceBench = (bestBenchIdx != SIZE_MAX && benchCount < maxBenches &&
                                      plan.totalCost + benchCost <= budgetMoney &&
                                      bestBenchScore >= kMinBenchScore);
                bool canPlaceBin = (bestBinIdx != SIZE_MAX && binCount < maxBins &&
                                    plan.totalCost + binCost <= budgetMoney &&
                                    bestBinScore >= kMinBinScore);

                if (canPlaceBench && canPlaceBin)
                {
                    // Both available - alternate, but skip if score is much lower
                    if (placeBenchNext)
                    {
                        // Prefer bench, but switch to bin if bench score is much lower
                        chosenType = (bestBenchScore >= bestBinScore * 0.5) ? "bench" : "bin";
                    }
                    else
                    {
                        // Prefer bin, but switch to bench if bin score is much lower
                        chosenType = (bestBinScore >= bestBenchScore * 0.5) ? "bin" : "bench";
                    }
                    placeBenchNext = !placeBenchNext;  // Alternate for next iteration
                }
                else if (canPlaceBench)
                {
                    chosenType = "bench";
                }
                else if (canPlaceBin)
                {
                    chosenType = "bin";
                }
                else
                {
                    break;  // Nothing more to place
                }

                if (chosenType == "bench")
                {
                    const auto& c = benchCandidates[bestBenchIdx];
                    uint64_t key = MakeTileKey(c.location);

                    PlannedPlacement p;
                    p.type = "bench";
                    p.location = c.location;
                    p.score = c.benchValue;
                    p.reason = c.primaryReason.empty() ? "good coverage" : c.primaryReason;
                    p.estimatedCost = static_cast<int>(benchCost / 10);

                    plan.placements.push_back(p);
                    plan.totalCost += benchCost;
                    usedTiles.insert(key);
                    benchCount++;

                    // Update nearby BENCH scores only (bins unaffected by bench placement)
                    ApplySpreadingPenalty(benchCandidates, c.location, true, c.freeEdgeCount);
                }
                else if (chosenType == "bin")
                {
                    const auto& c = binCandidates[bestBinIdx];
                    uint64_t key = MakeTileKey(c.location);

                    PlannedPlacement p;
                    p.type = "bin";
                    p.location = c.location;
                    p.score = c.binValue;
                    p.reason = c.primaryReason.empty() ? "litter coverage" : c.primaryReason;
                    p.estimatedCost = static_cast<int>(binCost / 10);

                    plan.placements.push_back(p);
                    plan.totalCost += binCost;
                    usedTiles.insert(key);
                    binCount++;

                    // Update nearby BIN scores only (benches unaffected by bin placement)
                    ApplySpreadingPenalty(binCandidates, c.location, false, c.freeEdgeCount);
                }
            }

            // Handle bathrooms separately (building placement, not path additions)
            for (const auto& c : cache.buildingCandidates)
            {
                if (bathroomCount >= maxBathrooms || plan.totalCost + bathroomCost > budgetMoney)
                    break;

                PlannedPlacement p;
                p.type = "bathroom";
                p.location = c.location;
                p.score = c.score;
                p.reason = c.reason.empty() ? "coverage" : c.reason;
                p.estimatedCost = static_cast<int>(bathroomCost / 10);

                plan.placements.push_back(p);
                plan.totalCost += bathroomCost;
                bathroomCount++;
            }

            // Generate plan ID and store
            plan.id = "plan_" + std::to_string(g_nextPlanId++);
            plan.summary = std::to_string(benchCount) + " benches, " +
                          std::to_string(binCount) + " bins, " +
                          std::to_string(bathroomCount) + " bathrooms";

            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                CleanupOldPlans();  // Remove expired plans before adding new one
                g_recommendationPlans[plan.id] = plan;
            }

            // Build response (convert money64 to display currency by dividing by 10)
            json_t payload = json_t::object();
            payload["planId"] = plan.id;
            payload["summary"] = plan.summary;
            payload["totalCost"] = static_cast<int>(plan.totalCost / 10);
            payload["budget"] = budget;
            payload["remainingBudget"] = budget - static_cast<int>(plan.totalCost / 10);

            json_t placements = json_t::array();
            for (const auto& p : plan.placements)
            {
                json_t item = json_t::object();
                item["type"] = p.type;
                item["x"] = p.location.x;
                item["y"] = p.location.y;
                item["z"] = p.location.z;
                item["score"] = p.score;
                item["reason"] = p.reason;
                item["cost"] = p.estimatedCost;
                placements.push_back(item);
            }
            payload["placements"] = placements;

            json_t counts = json_t::object();
            counts["benches"] = benchCount;
            counts["bins"] = binCount;
            counts["bathrooms"] = bathroomCount;
            payload["counts"] = counts;

            // Add warnings for any unknown priorities
            if (!unknownPriorities.empty())
            {
                json_t warnings = json_t::array();
                for (const auto& up : unknownPriorities)
                {
                    warnings.push_back("Unknown priority type ignored: '" + up +
                        "'. Valid types are: benches, bins, bathrooms");
                }
                payload["warnings"] = warnings;
            }

            return RpcResult::Ok(payload);
        }

        RpcResult HandleAmenitiesAuto(const json_t& params)
        {
            auto budgetParam = GetIntParam(params, "budget");
            if (!budgetParam)
                return RpcResult::Error(kErrorInvalidParams, "budget parameter is required");

            int budget = *budgetParam;

            auto* context = GetContext();
            if (context == nullptr)
                return RpcResult::Error(kErrorInvalidParams, "Game context not available");

            // First generate recommendation
            auto recommendResult = HandleAmenitiesRecommend(params);
            if (!recommendResult.success)
                return recommendResult;

            auto recommendPayload = recommendResult.payload;
            std::string planId = recommendPayload["planId"].get<std::string>();

            // Now execute the plan
            RecommendationPlan plan;
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                auto it = g_recommendationPlans.find(planId);
                if (it == g_recommendationPlans.end())
                    return RpcResult::Error(kErrorInvalidParams, "Plan not found");
                plan = it->second;
            }

            auto& objManager = context->GetObjectManager();
            auto benchEntry = objManager.GetLoadedObjectEntryIndex(kBenchIdentifier);
            auto binEntry = objManager.GetLoadedObjectEntryIndex(kBinIdentifier);

            json_t placed = json_t::array();
            json_t failed = json_t::array();
            int placedBenches = 0, placedBins = 0, placedBathrooms = 0;
            int totalSpent = 0;

            for (const auto& p : plan.placements)
            {
                if (p.type == "bench")
                {
                    if (benchEntry == kObjectEntryIndexNull)
                        continue;

                    CoordsXYZ loc{ p.location.x * 32, p.location.y * 32, p.location.z * 8 };
                    auto action = GameActions::FootpathAdditionPlaceAction(loc, benchEntry);
                    auto result = GameActions::Execute(&action, getGameState());

                    if (result.error == GameActions::Status::ok)
                    {
                        json_t item = json_t::object();
                        item["type"] = "bench";
                        item["x"] = p.location.x;
                        item["y"] = p.location.y;
                        item["reason"] = p.reason;
                        placed.push_back(item);
                        placedBenches++;
                        totalSpent += p.estimatedCost;  // Use actual cost from plan
                    }
                    else
                    {
                        json_t item = json_t::object();
                        item["type"] = "bench";
                        item["x"] = p.location.x;
                        item["y"] = p.location.y;
                        std::string errorMsg = result.getErrorMessage();
                        item["error"] = errorMsg.empty() ? "placement failed" : errorMsg;
                        failed.push_back(item);
                    }
                }
                else if (p.type == "bin")
                {
                    if (binEntry == kObjectEntryIndexNull)
                        continue;

                    CoordsXYZ loc{ p.location.x * 32, p.location.y * 32, p.location.z * 8 };
                    auto action = GameActions::FootpathAdditionPlaceAction(loc, binEntry);
                    auto result = GameActions::Execute(&action, getGameState());

                    if (result.error == GameActions::Status::ok)
                    {
                        json_t item = json_t::object();
                        item["type"] = "bin";
                        item["x"] = p.location.x;
                        item["y"] = p.location.y;
                        item["reason"] = p.reason;
                        placed.push_back(item);
                        placedBins++;
                        totalSpent += p.estimatedCost;  // Use actual cost from plan
                    }
                    else
                    {
                        json_t item = json_t::object();
                        item["type"] = "bin";
                        item["x"] = p.location.x;
                        item["y"] = p.location.y;
                        std::string errorMsg = result.getErrorMessage();
                        item["error"] = errorMsg.empty() ? "placement failed" : errorMsg;
                        failed.push_back(item);
                    }
                }
                else if (p.type == "bathroom")
                {
                    auto& gameState = getGameState();
                    ride_type_t toiletType = RIDE_TYPE_TOILETS;
                    const auto& rtd = GetRideTypeDescriptor(toiletType);

                    int32_t colour1 = RideGetRandomColourPresetIndex(toiletType);
                    int32_t colour2 = RideGetUnusedPresetVehicleColour(0);

                    auto rideCreate = GameActions::RideCreateAction(
                        toiletType, 0, colour1, colour2, gameState.lastEntranceStyle, RideInspection::every10Minutes);
                    auto createResult = GameActions::Execute(&rideCreate, gameState);

                    if (createResult.error != GameActions::Status::ok)
                    {
                        json_t item = json_t::object();
                        item["type"] = "bathroom";
                        item["x"] = p.location.x;
                        item["y"] = p.location.y;
                        item["error"] = "failed to create ride";
                        failed.push_back(item);
                        continue;
                    }

                    RideId rideId = createResult.getData<RideId>();

                    // Find direction from building candidates
                    Direction facing = Direction(0);
                    for (const auto& bc : GetCachedAnalysis().buildingCandidates)
                    {
                        if (bc.location.x == p.location.x && bc.location.y == p.location.y)
                        {
                            facing = bc.direction;
                            break;
                        }
                    }

                    CoordsXYZD origin{ p.location.x * 32, p.location.y * 32, p.location.z * 8, facing };
                    SelectedLiftAndInverted liftFlags{};
                    auto trackAction = GameActions::TrackPlaceAction(
                        rideId, rtd.StartTrackPiece, toiletType, origin, 0, 0, 0, liftFlags, false);
                    auto placeResult = GameActions::Execute(&trackAction, gameState);

                    if (placeResult.error != GameActions::Status::ok)
                    {
                        auto demolish = GameActions::RideDemolishAction(rideId, GameActions::RideModifyType::demolish);
                        GameActions::Execute(&demolish, gameState);

                        json_t item = json_t::object();
                        item["type"] = "bathroom";
                        item["x"] = p.location.x;
                        item["y"] = p.location.y;
                        item["error"] = "failed to place building";
                        failed.push_back(item);
                        continue;
                    }

                    json_t item = json_t::object();
                    item["type"] = "bathroom";
                    item["x"] = p.location.x;
                    item["y"] = p.location.y;
                    item["reason"] = p.reason;
                    item["rideId"] = static_cast<int>(rideId.ToUnderlying());
                    placed.push_back(item);
                    placedBathrooms++;
                    totalSpent += p.estimatedCost;  // Use actual cost from plan
                }
            }

            // Invalidate cache since park changed
            g_analysisCache.Invalidate();

            // Build response
            json_t payload = json_t::object();
            payload["placed"] = placed;
            payload["failed"] = failed;
            payload["totalSpent"] = totalSpent;
            payload["budget"] = budget;

            json_t counts = json_t::object();
            counts["benches"] = placedBenches;
            counts["bins"] = placedBins;
            counts["bathrooms"] = placedBathrooms;
            payload["counts"] = counts;

            // Build summary explanation
            std::string summary = "Placed " + std::to_string(placedBenches) + " benches, " +
                                 std::to_string(placedBins) + " bins, " +
                                 std::to_string(placedBathrooms) + " bathrooms. ";
            summary += "Spent $" + std::to_string(totalSpent) + " of $" + std::to_string(budget) + " budget.";
            payload["summary"] = summary;

            return RpcResult::Ok(payload);
        }

        // Legacy handlers for backward compatibility
        RpcResult HandleAmenitiesPlaceBenches(const json_t& params)
        {
            auto countParam = GetIntParam(params, "count");
            int count = countParam.value_or(5);

            auto* context = GetContext();
            if (context == nullptr)
                return RpcResult::Error(kErrorInvalidParams, "Game context not available");

            auto& objManager = context->GetObjectManager();
            auto entryIndex = objManager.GetLoadedObjectEntryIndex(kBenchIdentifier);
            if (entryIndex == kObjectEntryIndexNull)
                return RpcResult::Error(kErrorInvalidParams, "Bench object not loaded");

            const auto& cache = GetCachedAnalysis();

            // Build candidates for benches specifically (respects slope restrictions)
            std::vector<AmenityHeatValue> candidates;
            for (const auto& h : cache.heatMap)
            {
                if (h.canPlaceBench)
                    candidates.push_back(h);
            }

            json_t placed = json_t::array();
            json_t failed = json_t::array();
            int placedCount = 0;

            // Greedy placement: pick best, place, penalize nearby, repeat
            while (placedCount < count)
            {
                // Find best remaining candidate
                auto bestIt = std::max_element(candidates.begin(), candidates.end(),
                    [](const AmenityHeatValue& a, const AmenityHeatValue& b) {
                        return a.benchValue < b.benchValue;
                    });

                if (bestIt == candidates.end() || bestIt->benchValue <= kMinBenchScore)
                    break;

                const auto& c = *bestIt;
                CoordsXYZ loc{ c.location.x * 32, c.location.y * 32, c.location.z * 8 };
                auto action = GameActions::FootpathAdditionPlaceAction(loc, entryIndex);
                auto result = GameActions::Execute(&action, getGameState());

                if (result.error == GameActions::Status::ok)
                {
                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    item["z"] = c.location.z;
                    item["value"] = c.benchValue;
                    item["reason"] = c.primaryReason.empty() ? "optimal location" : c.primaryReason;
                    placed.push_back(item);
                    placedCount++;

                    // Update nearby bench scores
                    ApplySpreadingPenalty(candidates, c.location, true, c.freeEdgeCount);
                }
                else
                {
                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    std::string errorMsg = result.getErrorMessage();
                    item["error"] = errorMsg.empty() ? "placement failed" : errorMsg;
                    failed.push_back(item);
                }

                // Mark this tile as used by setting score to -1
                bestIt->benchValue = -1;
            }

            g_analysisCache.Invalidate();

            json_t payload = json_t::object();
            payload["type"] = "Benches";
            payload["placed"] = placed;
            payload["placedCount"] = placedCount;
            payload["requestedCount"] = count;
            if (!failed.empty())
                payload["failed"] = failed;

            return RpcResult::Ok(payload);
        }

        RpcResult HandleAmenitiesPlaceBins(const json_t& params)
        {
            auto countParam = GetIntParam(params, "count");
            int count = countParam.value_or(5);

            auto* context = GetContext();
            if (context == nullptr)
                return RpcResult::Error(kErrorInvalidParams, "Game context not available");

            auto& objManager = context->GetObjectManager();
            auto entryIndex = objManager.GetLoadedObjectEntryIndex(kBinIdentifier);
            if (entryIndex == kObjectEntryIndexNull)
                return RpcResult::Error(kErrorInvalidParams, "Bin object not loaded");

            const auto& cache = GetCachedAnalysis();

            // Build candidates for bins specifically (can go on slopes)
            std::vector<AmenityHeatValue> candidates;
            for (const auto& h : cache.heatMap)
            {
                if (h.canPlaceBin)
                    candidates.push_back(h);
            }

            json_t placed = json_t::array();
            json_t failed = json_t::array();
            int placedCount = 0;

            // Greedy placement: pick best, place, penalize nearby, repeat
            while (placedCount < count)
            {
                // Find best remaining candidate
                auto bestIt = std::max_element(candidates.begin(), candidates.end(),
                    [](const AmenityHeatValue& a, const AmenityHeatValue& b) {
                        return a.binValue < b.binValue;
                    });

                if (bestIt == candidates.end() || bestIt->binValue <= kMinBinScore)
                    break;

                const auto& c = *bestIt;
                CoordsXYZ loc{ c.location.x * 32, c.location.y * 32, c.location.z * 8 };
                auto action = GameActions::FootpathAdditionPlaceAction(loc, entryIndex);
                auto result = GameActions::Execute(&action, getGameState());

                if (result.error == GameActions::Status::ok)
                {
                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    item["z"] = c.location.z;
                    item["value"] = c.binValue;
                    item["reason"] = c.primaryReason.empty() ? "litter coverage" : c.primaryReason;
                    placed.push_back(item);
                    placedCount++;

                    // Update nearby bin scores
                    ApplySpreadingPenalty(candidates, c.location, false, c.freeEdgeCount);
                }
                else
                {
                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    std::string errorMsg = result.getErrorMessage();
                    item["error"] = errorMsg.empty() ? "placement failed" : errorMsg;
                    failed.push_back(item);
                }

                // Mark this tile as used by setting score to -1
                bestIt->binValue = -1;
            }

            g_analysisCache.Invalidate();

            json_t payload = json_t::object();
            payload["type"] = "Bins";
            payload["placed"] = placed;
            payload["placedCount"] = placedCount;
            payload["requestedCount"] = count;
            if (!failed.empty())
                payload["failed"] = failed;

            return RpcResult::Ok(payload);
        }

        RpcResult HandleAmenitiesPlaceBathrooms(const json_t& params)
        {
            auto countParam = GetIntParam(params, "count");
            int count = countParam.value_or(1);

            const auto& cache = GetCachedAnalysis();
            auto& gameState = getGameState();

            ride_type_t toiletType = RIDE_TYPE_TOILETS;
            const auto& rtd = GetRideTypeDescriptor(toiletType);

            json_t placed = json_t::array();
            json_t failed = json_t::array();
            int placedCount = 0;

            for (const auto& c : cache.buildingCandidates)
            {
                if (placedCount >= count)
                    break;

                int32_t colour1 = RideGetRandomColourPresetIndex(toiletType);
                int32_t colour2 = RideGetUnusedPresetVehicleColour(0);

                auto rideCreate = GameActions::RideCreateAction(
                    toiletType, 0, colour1, colour2, gameState.lastEntranceStyle, RideInspection::every10Minutes);
                auto createResult = GameActions::Execute(&rideCreate, gameState);

                if (createResult.error != GameActions::Status::ok)
                {
                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    item["error"] = "failed to create ride";
                    failed.push_back(item);
                    continue;
                }

                RideId rideId = createResult.getData<RideId>();

                CoordsXYZD origin{ c.location.x * 32, c.location.y * 32, c.location.z * 8, c.direction };
                SelectedLiftAndInverted liftFlags{};
                auto trackAction = GameActions::TrackPlaceAction(
                    rideId, rtd.StartTrackPiece, toiletType, origin, 0, 0, 0, liftFlags, false);
                auto placeResult = GameActions::Execute(&trackAction, gameState);

                if (placeResult.error != GameActions::Status::ok)
                {
                    auto demolish = GameActions::RideDemolishAction(rideId, GameActions::RideModifyType::demolish);
                    GameActions::Execute(&demolish, gameState);

                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    item["error"] = "failed to place building";
                    failed.push_back(item);
                    continue;
                }

                json_t item = json_t::object();
                item["x"] = c.location.x;
                item["y"] = c.location.y;
                item["z"] = c.location.z;
                item["value"] = c.score;
                item["reason"] = c.reason.empty() ? "coverage" : c.reason;
                item["rideId"] = static_cast<int>(rideId.ToUnderlying());
                placed.push_back(item);
                placedCount++;
            }

            g_analysisCache.Invalidate();

            json_t payload = json_t::object();
            payload["type"] = "Bathrooms";
            payload["placed"] = placed;
            payload["placedCount"] = placedCount;
            payload["requestedCount"] = count;
            if (!failed.empty())
                payload["failed"] = failed;

            return RpcResult::Ok(payload);
        }

        RpcResult HandleAmenitiesPlaceFirstAid(const json_t& params)
        {
            auto countParam = GetIntParam(params, "count");
            int count = countParam.value_or(1);

            const auto& cache = GetCachedAnalysis();
            auto& gameState = getGameState();

            ride_type_t firstAidType = RIDE_TYPE_FIRST_AID;
            const auto& rtd = GetRideTypeDescriptor(firstAidType);

            json_t placed = json_t::array();
            json_t failed = json_t::array();
            int placedCount = 0;

            for (const auto& c : cache.buildingCandidates)
            {
                if (placedCount >= count)
                    break;

                int32_t colour1 = RideGetRandomColourPresetIndex(firstAidType);
                int32_t colour2 = RideGetUnusedPresetVehicleColour(0);

                auto rideCreate = GameActions::RideCreateAction(
                    firstAidType, 0, colour1, colour2, gameState.lastEntranceStyle, RideInspection::every10Minutes);
                auto createResult = GameActions::Execute(&rideCreate, gameState);

                if (createResult.error != GameActions::Status::ok)
                {
                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    item["error"] = "failed to create ride";
                    failed.push_back(item);
                    continue;
                }

                RideId rideId = createResult.getData<RideId>();

                CoordsXYZD origin{ c.location.x * 32, c.location.y * 32, c.location.z * 8, c.direction };
                SelectedLiftAndInverted liftFlags{};
                auto trackAction = GameActions::TrackPlaceAction(
                    rideId, rtd.StartTrackPiece, firstAidType, origin, 0, 0, 0, liftFlags, false);
                auto placeResult = GameActions::Execute(&trackAction, gameState);

                if (placeResult.error != GameActions::Status::ok)
                {
                    auto demolish = GameActions::RideDemolishAction(rideId, GameActions::RideModifyType::demolish);
                    GameActions::Execute(&demolish, gameState);

                    json_t item = json_t::object();
                    item["x"] = c.location.x;
                    item["y"] = c.location.y;
                    item["error"] = "failed to place building";
                    failed.push_back(item);
                    continue;
                }

                json_t item = json_t::object();
                item["x"] = c.location.x;
                item["y"] = c.location.y;
                item["z"] = c.location.z;
                item["value"] = c.score;
                item["reason"] = c.reason.empty() ? "nausea coverage" : c.reason;
                item["rideId"] = static_cast<int>(rideId.ToUnderlying());
                placed.push_back(item);
                placedCount++;
            }

            g_analysisCache.Invalidate();

            json_t payload = json_t::object();
            payload["type"] = "First Aid";
            payload["placed"] = placed;
            payload["placedCount"] = placedCount;
            payload["requestedCount"] = count;
            if (!failed.empty())
                payload["failed"] = failed;

            return RpcResult::Ok(payload);
        }

        RpcResult HandleAmenitiesEstimate(const json_t& params)
        {
            const auto& cache = GetCachedAnalysis();

            int existingBenches = 0, existingBins = 0, placeableTiles = 0;
            for (const auto& path : cache.paths)
            {
                existingBenches += path.existingBenchCount;
                existingBins += path.existingBinCount;
                if (path.canPlaceBench || path.canPlaceBin)
                    placeableTiles++;
            }

            double nauseaFactor = 0.0;
            for (const auto& exit : cache.nauseousExits)
            {
                nauseaFactor += exit.nauseaRating;
            }

            double foodFactor = static_cast<double>(cache.foodShops.size()) * 0.5;

            int recommendedBenches = static_cast<int>(
                (cache.paths.size() / 12.0) + (nauseaFactor * 5.0) + (foodFactor * 0.5));
            int recommendedBins = static_cast<int>(
                (cache.paths.size() / 15.0) + (nauseaFactor * 8.0) + (foodFactor * 2.0));
            int recommendedBathrooms = static_cast<int>(
                std::max(1.0, (cache.paths.size() / 50.0) + (nauseaFactor * 0.5)));
            int recommendedFirstAid = static_cast<int>(
                std::max(0.0, nauseaFactor * 0.3));

            json_t payload = json_t::object();

            json_t current = json_t::object();
            current["benches"] = existingBenches;
            current["bins"] = existingBins;
            payload["current"] = current;

            json_t recommended = json_t::object();
            recommended["benches"] = recommendedBenches;
            recommended["bins"] = recommendedBins;
            recommended["bathrooms"] = recommendedBathrooms;
            recommended["firstAid"] = recommendedFirstAid;
            payload["recommended"] = recommended;

            json_t needed = json_t::object();
            needed["benches"] = std::max(0, recommendedBenches - existingBenches);
            needed["bins"] = std::max(0, recommendedBins - existingBins);
            needed["bathrooms"] = recommendedBathrooms;
            needed["firstAid"] = recommendedFirstAid;
            payload["needed"] = needed;

            payload["placeableTiles"] = placeableTiles;
            payload["totalPathTiles"] = static_cast<int>(cache.paths.size());

            // Reasoning for Claude
            json_t reasoning = json_t::object();
            reasoning["nauseousRides"] = static_cast<int>(cache.nauseousExits.size());
            reasoning["foodShops"] = static_cast<int>(cache.foodShops.size());
            reasoning["nauseaFactor"] = nauseaFactor;
            payload["reasoning"] = reasoning;

            return RpcResult::Ok(payload);
        }

        // =========================================================================
        // Handler Registration
        // =========================================================================

        struct AmenityHandlerRegistrar
        {
            AmenityHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();

                registry.Register("amenities.analyze", HandleAmenitiesAnalyze);
                registry.Register("amenities.estimate", HandleAmenitiesEstimate);
                registry.Register("amenities.recommend", HandleAmenitiesRecommend);
                registry.Register("amenities.auto", HandleAmenitiesAuto);
                registry.Register("amenities.placeBenches", HandleAmenitiesPlaceBenches);
                registry.Register("amenities.placeBins", HandleAmenitiesPlaceBins);
                registry.Register("amenities.placeBathrooms", HandleAmenitiesPlaceBathrooms);
                registry.Register("amenities.placeFirstAid", HandleAmenitiesPlaceFirstAid);
            }
        } static amenityRegistrar;

    } // anonymous namespace

    void InitAmenityHandlers()
    {
        (void)amenityRegistrar;
    }

} // namespace OpenRCT2::Scripting::Rpc::Handlers

#endif // ENABLE_SCRIPTING

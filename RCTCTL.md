# rctctl CLI

Command-line interface for controlling OpenRCT2 from the AI agent terminal.

## Design Principles

- The CLI wraps game UI functionality with text-based ergonomics
- No image-based "vision" — everything is text
- Strong help text enables the agent to self-navigate
- Intended for AI agent use; auto-completion not needed

Legend: `[x]` = implemented in the current agent_bundle build.

## CLI Pattern

Ergonomics should be consistent.

- Patterned after `kubectl`/`gh`
- resource-first command tree: `rctctl <resource> <verb> [subverb] [args]` (resource nouns = park, map, ride, staff, research, etc.) get their own tree.
- Shared verbs across resources: `status`, `list`, `get`, `set`, `open`, `close`, `history`, `watch` to make behavior guessable.
- Each command described by a registry entry (summary, help, args, default renderer) so help text + parsing stay uniform.
- Output contract: natural-language text by default (Claude/front-end first).
- Transport layer: verbs marshal directly to JSON-RPC (read for `status/list/history`, write for `set/open/...`) with zero bespoke logic in CLI.

Exceptions are possible, but should be rare.

### Code layout (rctctl/)
- `src/main.cpp` — wires CLI, registry, and RPC together
- `src/cli/` — argument parsing and helper utilities
- `src/rpc/` — TCP/JSON-RPC transport
- `src/commands/` — per-resource command implementations
- `src/renderers/` — JSON-to-text output formatting
- `include/rctctl/` — shared headers (cli, commands, renderers, rpc, util)

### Serialization patterns
When serializing game data with references (thoughts → items/rides, actions → targets), always include the **resolved display text**, not just type codes or IDs. For example, guest thoughts like "I've already got" must append the item name ("I've already got a burger"). Use helpers like `FormatThoughtText()` in the RPC handlers (`src/openrct2/scripting/rpc/`). The agent cannot look up internal IDs—surface complete, human-readable strings.

### Z-coordinate units
All user-facing z-coordinates (heights) must use **tile units**, not world units. The game internally uses world units (8x larger), but agents need consistent values across commands. Use `WorldZToTileZ()` for output and `TileZToWorldZ()` for input in the RPC handlers. Ground level is typically z=14 in tile units.

## Spirit of the CLI

The CLI does not give Claude Code super powers, or 'god mode'. It simply provides an interface for the bot to play the game, like a user.

The purpose of this experiment as a whole is to feel as if Claude is 'playing the game with us' and helping us as a operating partner.

Input values should feel like their in-game 'display name' equivalents, and not expose internal key/path styling conventions to the user-facing experience

## Output Formatting

To better match the natural language interface used by an LLM (Text-based User Interface or TUI), and more fairly align Claude's interface to the game with the user's interface to the game, CLI
outputs should be formatted in a 'front-end layer' manner as natural language interfaces.

Tabular data should be printed in a simple table text format.

Structured outputs should be printed as labels and values without extra operator symbols.

## Follow Mode Hints

The agent window allows the user to toggle on "follow mode" in which each `rctctl` command run by Claude Code moves the viewport, or opens a window (1 window can be opened at at time).

This allows the player to 'follow along'.

- Every JSON-RPC method (and therefore every `rctctl` command) can now attach a `followHint` when returning `RpcResult::Ok(...)`. These hints are relayed back to the CLI response, logged in the Claude Activity Feed, and consumed by `ClaudeFollowController` so that, when follow mode is enabled, the camera pans/zooms and the correct window/tab is focused without extra commands.
- A hint carries three pieces of metadata: a `contextLabel` (one-sentence narration for the sidecar), optional `camera` coordinates, and a `window` intent (`ride`, `staff`, `guest`, `park`, `constructRide`, `rideList`, or a generic `window` plus class name). `requestCameraFocus`/`requestWindowFocus` booleans let non-spatial commands opt out.
- Coverage matters. Entity-centric verbs (rides, staff, guests, shops, land) should provide precise tiles/entity IDs so follow mode can jump straight there; list/status verbs should at minimum surface the relevant HUD (map, staff list, finances, etc.). When you add or expand a command, wire it into the existing `Make*Hint` helpers in `RpcUtils.h` so we keep parity between CLI verbs and viewport intent.
- Follow mode itself is exposed over RPC via `claude.follow.getMode|setMode`, so headless automation (including `rctctl`) can toggle it; assume the UI will honor hints automatically once mode is on, so no secondary "locate" verbs are needed.

## Readiness Legend

Each command tracks three essential features:

**Viewport** – Does the command emit a `followHint` that moves the viewport or focuses the right window?
- `Y` – emits followHint
- `N` – should provide viewport cues but does not yet
- `S` – deliberately skips viewport movement
- `TBD` – not audited yet

**Help** – Does the command expose clear, layered help text?
- `Stacked` – layered help (resource overview, verb synopsis, flag detail)
- `Single` – single-level help but still clear
- `Needs` – missing critical context or structure
- `TBD` – not reviewed

**Errors** – Do validation errors provide actionable guidance?
- `Guided` – errors point to likely fixes or suggest valid values
- `Needs` – errors fall through to raw RPC or exception output
- `TBD` – not reviewed

## Park

- [x] `park status` **[Viewport: Y | Help: Single | Errors: S]** — Scenario + finance pulse: surfaces scenario/goal text, open state, guests (in park + heading), park rating, in-game date, cash on hand, park/company value, current loan, operating profit (this vs last month), marketing/research spend, park boundary bounding box (top/left/right/bottom extents in tile coordinates), total park area in tiles, and entrance coordinates for quick spatial orientation.
- [x] `park info` **[Viewport: Y | Help: Single | Errors: S]** — Alias for `park status`.
- [x] `park guests` **[Viewport: Y | Help: Single | Errors: S]** — Guest traffic: snapshots guests currently inside the park, guests en route to the entrance, and the live park rating for quick morale checks.
- [x] `park price` **[Viewport: Y | Help: Single | Errors: S]** — Shows current park entrance price.
- [x] `park price set` **[Viewport: Y | Help: Single | Errors: Guided]** — Sets park entrance price. Requires `--value <amount>`. Values above max are clamped. CLI validates numeric input and provides clear error if flag missing.
- [x] `park open` **[Viewport: Y | Help: Single | Errors: Guided]** — Opens the park gates. Returns GameAction errors if action fails.
- [x] `park close` **[Viewport: Y | Help: Single | Errors: Guided]** — Closes the park gates. Returns GameAction errors if action fails.
- [x] `park rating history` **[Viewport: Y | Help: Single | Errors: S]** — Reputation timelines: streams the monthly rating timeline.
- [x] `park warnings` **[Viewport: Y | Help: Single | Errors: S]** — Park health: reports live litter/vandalism/ride breakdown buckets plus queue hotspots.

## Map

- [x] `map status` **[Viewport: S | Help: Single | Errors: S]** — Width/height, owned tiles vs construction rights, water coverage, min/max terrain heights.
- [x] `map tile` **[Viewport: S | Help: Single | Errors: Guided]** — Per-tile ownership + rights flags, surface + edge objects, water depth, terrain heights, path/queue geometry with edge connectivity (N/E/S/W directions), queue-to-ride associations, track/ride references, entrances, banners/walls/scenery, and staff patrol overlaps. Requires `--x --y`. CLI validates coordinates are non-negative.
- [x] `map area` **[Viewport: Y | Help: Single | Errors: Guided]** — ASCII mini-map showing a 16×16 grid anchored at the given top-left tile with inline legend. Requires `--x --y`. CLI validates coordinates are non-negative.
- [x] `map area paths` **[Viewport: Y | Help: Single | Errors: Guided]** — Filtered 16×16 view showing only footpaths (P), queues (Q), or empty (.). Requires `--x --y`.
- [x] `map area rides` **[Viewport: Y | Help: Single | Errors: Guided]** — Filtered 16×16 view showing only ride tracks (R), entrances (E), or empty (.). Requires `--x --y`.
- [x] `map area ownership` **[Viewport: Y | Help: Single | Errors: Guided]** — Filtered 16×16 view showing only land ownership: owned (O), construction rights (c), or unowned (#). Requires `--x --y`.
- [x] `map area scenery` **[Viewport: Y | Help: Single | Errors: Guided]** — Filtered 16×16 view showing only trees (T), scenery (S), or empty (.). Requires `--x --y`.
- [x] `map area water` **[Viewport: Y | Help: Single | Errors: Guided]** — Filtered 16×16 view showing only water (W) or land (.). Requires `--x --y`.
- [x] `map area shops` **[Viewport: Y | Help: Single | Errors: Guided]** — Filtered 16×16 view showing only shops/stalls (S) or empty (.). Requires `--x --y`.
- [x] `map heatmap guests` **[Viewport: Y | Help: Single | Errors: Guided]** — Guest density hotspots with optional limit. CLI validates limit is at least 1.
- [x] `map ownership` **[Viewport: S | Help: Single | Errors: S]** — Reports purchasable land rectangles (owned tiles vs construction rights) so Claude can reason about expansion limits.
- [x] `map scan development` **[Viewport: Y | Help: Single | Errors: Guided]** — Strategic development density scan. Renders a 16×16 grid where each cell aggregates infrastructure (rides, paths, shops, entrances) across a block of game tiles. Use `--zoom 10` (default, 10×10 tiles/cell) or `--zoom 20` (20×20 tiles/cell) to control resolution. Symbols indicate density: # = dense (75%+), + = high (50-75%), = = medium (25-50%), - = light (1-25%), . = undeveloped. Optional `--x --y` (defaults to park center). CLI validates coordinates are non-negative, zoom is 10 or 20.
- [x] `map scan guests` **[Viewport: Y | Help: Single | Errors: Guided]** — Strategic guest density scan. Renders a 16×16 grid where each cell aggregates guest counts across a block of game tiles. Use `--zoom 10` (default) or `--zoom 20` to control resolution. Symbols indicate traffic: # = very busy (20+ guests), + = busy (10-19), = = moderate (5-9), - = light (1-4), . = empty. Helps identify high-traffic park regions for navigation and planning. Optional `--x --y` (defaults to park center). CLI validates coordinates are non-negative, zoom is 10 or 20.

## Rides

- [x] `rides list` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Inventory: shows id, name, status, ride type, and approximate coordinates.
- [x] `rides get` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Detailed status: includes state, mode, trains/cars, queue tiles & wait, ratings, reliability/downtime, inspection intervals, mechanic dispatch state, profit/income/run cost, lifetime guests, on-ride photo flag, per-station entrance/exit coordinates and facing with path connectivity status (whether queue/footpath is connected), operation settings (operationOption/operationLabel/operationMin/operationMax), and departure settings (departureFlags/waitForLoad/leaveWhenAnotherArrives/syncWithAdjacentStations). Requires `--id` or `--name`.
- [x] `rides open` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Gate control: opens the ride. Returns semantic error if used on a shop/stall instead of a ride.
- [x] `rides close` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Gate control: closes the ride. Use `--evict-guests` to immediately remove all guests (like "close for repairs"). Returns semantic error if used on a shop/stall instead of a ride.
- [x] `rides test` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Gate control: sets ride to testing mode. Returns semantic error if used on a shop/stall instead of a ride.
- [x] `rides price` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Shows current (and secondary, if any) fares.
- [x] `rides price set` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Adjusts fares with before/after echoes. Requires `--value <amount> [--secondary true]`.
- [x] `rides tune` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Operating tweaks: adjusts mode, min-wait, num-circuits, lift-hill-speed, inspection intervals, operation-option (laps/launch-speed/rotations/time-limit), and departure settings (wait-for-load/leave-on-arrival/sync-stations).
- [x] `rides rename` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Renames the ride. Requires `--id` and `--name`.
- [x] `rides refurbish` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Restores a ride's reliability by resetting breakdown history. Safe operation that does not remove the ride. Requires `--id` or `--name`.
- [x] `rides demolish` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Permanently removes a ride. Destructive operation that cannot be undone. Requires `--id` or `--name`.
- [x] `rides catalog` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Blueprint browser: lists every invented (or optionally locked with `--all`) ride entry with category, base build cost, and default prices.
- [x] `rides place` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Ride deployment: instantiates a flat ride at the specified north-west tile, returning footprint geometry, cost breakdown, and entrance/exit candidate tiles. Requires `--type <object> --x --y [--z --facing]`.
- [x] `rides entrance place` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Station fixtures: drops entrance building next to ride footprint, auto-facing the ride (with optional overrides) and surfacing obstructions.
- [x] `rides exit place` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Station fixtures: drops exit building next to ride footprint, auto-facing the ride (with optional overrides) and surfacing obstructions.
- [x] `rides breakdowns` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Reliability snapshots: exposes current/pending breakdown reasons, mechanic dispatch state, downtime buckets, and inspection timers.
- [x] `rides throughput` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Throughput telemetry: mirrors the Customers tab (customers/hour, queue stats, popularity/satisfaction, sold items, recent intervals).
- [x] `rides feedback` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Guest sentiment: clusters the most common ride-specific thoughts with representative guest ids. Optional `[--limit --guest-limit]`.
- [x] `rides finances` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Ride finances: mirrors the ride list finance tab with income/hr, running cost/hr, and profit/hr columns for every ride so Claude can spot money sinks quickly, plus `--order`, `--direction`, `--status`, and `--limit` flags for slicing.
- [x] `rides perception` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Guest perception metrics: lists rides with popularity, satisfaction, favorites count, and ratings (excitement/intensity/nausea). Optional `[--order --direction --status --limit]`.
- [x] `rides operations` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Operational health metrics: lists rides with reliability, downtime, queue time, queue length, customers, and age. Optional `[--order --direction --status --limit]`.

### Pre-built Coasters

Pre-built coasters (`.td6` track design files) allow placing complete roller coasters and tracked rides without building piece by piece.

- [x] `rides coasters categories` **[Viewport: Y | Help: Single | Errors: Guided]** — Lists ride categories (Roller Coasters, Thrill Rides, etc.) that have pre-built coasters available, with design and type counts per category.
- [x] `rides coasters types` **[Viewport: Y | Help: Single | Errors: Guided]** — Lists ride types that have pre-built coasters, with invention status and design counts. Filter by category with `--category`. Optional `[--category]`.
- [x] `rides coasters list` **[Viewport: Y | Help: Single | Errors: Guided]** — Lists available pre-built coasters with ratings (excitement/intensity/nausea), space requirements, and invention status. Filter by ride type with `--type`. Optional `[--type]`.
- [x] `rides coasters preview` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Queries what would happen if a coaster were placed at a location. Returns cost estimate, feasibility, and any errors without actually placing. Requires `--name --x --y [--z --direction]`.
- [x] `rides coasters place` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Places a pre-built coaster at the specified location, creating a complete ride with track, vehicles, entrance, and exit. Optional scenery with `--scenery` flag. Requires `--name --x --y [--z --direction --scenery]`.

### Ride Theming

- [x] `rides theme get` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Shows all theming info: track colors (4 schemes), vehicle colors, vehicle color mode, and entrance style. Requires `--id` or `--name`.
- [x] `rides theme colors` **[Viewport: S | Help: Single | Errors: S]** — Lists all 56 available color names organized by category (classic, extended, special).
- [x] `rides theme track set` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Sets track colors for a color scheme. Supports main, additional, and supports colors. Requires `--id` or `--name` plus at least one color flag. Optional `[--scheme --main --additional --supports]`.
- [x] `rides theme vehicle set` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Sets vehicle colors for a train/vehicle index. Requires `--id` or `--name` plus at least one color flag. Optional `[--train --body --trim --tertiary]`.
- [x] `rides theme vehicle mode` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Sets vehicle color mode (same/per-train/per-car). Requires `--id` or `--name` and `--mode`.
- [x] `rides theme entrance list` **[Viewport: S | Help: Single | Errors: S]** — Lists available station entrance styles loaded in the current scenario.
- [x] `rides theme entrance set` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Sets entrance/station style. Requires `--id` or `--name` and `--style` (name or identifier).

## Shops & Facilities

- [x] `shops catalog` **[Viewport: Y | Help: Single | Errors: Needs]** — Lists every loaded shop/stall with the player-facing name, classification, stocked items, build cost, and ready-to-copy selectors (`--name`, `--entry-index`, `--type`) so Claude can paste exactly what the UI shows.
- [x] `shops list` **[Viewport: Y | Help: Single | Errors: Needs]** — Enumerates every placed shop/stall with tile coordinates, state, pricing, inventory, and throughput so Claude can spot underperformers.
- [x] `shops place` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Drops a shop/stall building at the specified tile (north-west anchor). Requires `--name|--entry-index|--type … --x --y [--z]`. The shop automatically faces toward an adjacent path and aligns to that path's height. An adjacent path is required; if none exists, the command returns an error. If `--z` is specified, there must be a path at that height.
- [x] `shops remove` **[Viewport: Y | Help: Single | Errors: Guided]** — Demolishes a stall either by ride identifier/name or by targeting the tile directly. Accepts `--ride-id|--ride-name|--x --y [--z]`.
- [x] `shops open` **[Viewport: Y | Help: Single | Errors: Guided]** — Opens a shop/stall to guests. Requires `--id` or `--name`. Returns semantic error if used on a ride instead of a shop.
- [x] `shops close` **[Viewport: Y | Help: Single | Errors: Guided]** — Closes a shop/stall. Requires `--id` or `--name`. Returns semantic error if used on a ride instead of a shop.
- [x] `shops price` **[Viewport: Y | Help: Single | Errors: Guided]** — Shows current item prices for a shop/stall along with default prices for comparison. Requires `--id` or `--name`.
- [x] `shops price set` **[Viewport: Y | Help: Single | Errors: Guided]** — Sets the price of items at a shop/stall. By default sets primary item price; use `--secondary=true` for shops with two items (e.g., Burger Bar). Requires `--id` or `--name` and `--value <amount>`. Optional `[--secondary]`.
- [x] `shops finances` **[Viewport: Y | Help: Single | Errors: Guided]** — Shop financial metrics: lists shops with profit, income, running cost, and total profit. Opens Shops and Stalls window. Optional `[--order --direction --limit]`.
- [x] `shops performance` **[Viewport: Y | Help: Single | Errors: Guided]** — Shop performance metrics: lists shops with popularity, satisfaction, and customer counts. Opens Shops and Stalls window. Optional `[--order --direction --limit]`.

## Facilities

Facilities include kiosks, toilets, ATMs, and first aid stations - infrastructure that serves guests but isn't a shop or ride.

- [x] `facilities list` **[Viewport: Y | Help: Single | Errors: Guided]** — Lists kiosks, toilets, ATMs, and first aid stations with their status and performance metrics.
- [x] `facilities finances` **[Viewport: Y | Help: Single | Errors: Guided]** — Facility financial metrics: lists facilities with profit, income, running cost, and total profit. Opens Kiosks and Facilities window. Optional `[--order --direction --limit]`.
- [x] `facilities performance` **[Viewport: Y | Help: Single | Errors: Guided]** — Facility performance metrics: lists facilities with popularity, satisfaction, and customer counts. Opens Kiosks and Facilities window. Optional `[--order --direction --limit]`.

## Guests

- [x] `guests list` **[Viewport: Y | Help: Single | Errors: Needs]** — Id, name, and state (entering/in-park/leaving) for guests in the park. Optional `[--limit]`.
- [x] `guests get` **[Viewport: Y | Help: Single | Errors: Guided]** — Full needs block (happiness/hunger/thirst/nausea/energy/toilet), wallet breakdown (cash + spend per category), ride references (favourite/previous/target), queue timer, carried items, and complete thought stream. Requires `--id`.
- [x] `guests search` **[Viewport: Y | Help: Single | Errors: Guided]** — Combines substring filtering with optional tile brush/cursor paging to find specific clusters of guests. Optional `[--name --x --y --radius --after --limit --include-outside]`.
- [x] `guests thoughts` **[Viewport: Y | Help: Single | Errors: Guided]** — Aggregates the most common thoughts, returning counts, ride context, and representative guest samples with optional sorting (count/text/ride) plus a ride-only filter. Optional `[--limit --guest-limit --order --direction --ride-only]`.
- [x] `guests moods` **[Viewport: Y | Help: Single | Errors: Guided]** — Buckets guests into labeled happiness bands (ecstatic → furious) with counts, averages, ranges, and sample ids, and can focus on specific mood keys. Optional `[--limit --guest-limit --order --direction --bands]`.
- [x] `guests pickup` **[Viewport: Y | Help: Single | Errors: Guided]** — Picks up a guest for manual placement. Requires `--id`.
- [x] `guests place` **[Viewport: Y | Help: Single | Errors: Guided]** — Places a picked-up guest at the specified tile. Requires `--id --x --y [--z]`.
- [x] `guests drop` **[Viewport: Y | Help: Single | Errors: Guided]** — Emergency recovery: returns a guest stuck in 'picked' state to their original location. Requires `--id`.

## Staff

- [x] `staff list` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Paged roster with id, role, name, state, tile coords, wage, hire month, and flexible sorting/filtering to spotlight tired mechanics or expensive entertainers. Optional `[--role --order --direction --limit]`.
- [x] `staff get` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Deep dive plus patrol sample (first 256 tiles) when assigned. Requires `--id`.
- [x] `staff hire` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Hire action backed by StaffHireNewAction (auto-places new staff at default spawn).
- [x] `staff fire` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Fire action backed by StaffFireAction.
- [x] `staff patrol` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Wraps StaffSetPatrolAreaAction for rectangular brushes plus clear/unset modes. Requires `--id --x --y [--width --height --mode]`.
- [x] `staff orders` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Toggles handyman/mechanic orders via StaffSetOrdersAction. Requires `--id (--sweeping=true --inspect=false ...)`.
- [x] `staff pickup` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Mirrors the in-game hand tool using PeepPickupAction so Claude can relocate staff.
- [x] `staff place` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Mirrors the in-game hand tool using PeepPickupAction so Claude can relocate staff.
- [x] `staff drop` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Mirrors the in-game hand tool using PeepPickupAction so Claude can relocate staff.

## Research

- [x] `research status` **[Viewport: Y | Help: Single | Errors: Guided]** — Funding level/index, progress %, expected month/day, per-category priority flags, next + last discovery metadata, and a filterable/sortable queue view. Optional `[--queue-limit --queue-category --queue-order --queue-direction]`. Opens research window, skips camera focus. Validates order fields (scenario/name/category), direction (asc/desc), queue limit positive, and category names.
- [x] `research set` **[Viewport: Y | Help: Single | Errors: Guided]** — Mutates funding tier and category mask. Optional `[--funding/--priorities]`. Opens research window, skips camera focus. Validates funding level strings (none/min/normal/max), category array format, and returns GameAction errors if action fails.

## Marketing

- [x] `marketing status` **[Viewport: Y | Help: Single | Errors: Guided]** — Enumerates every active campaign, remaining weeks, ride target (if any), and shop item labels for free-food pushes with optional sorting/filtering (weeks/type/target). Optional `[--order --direction --type --limit]`. Opens marketing window (finances), skips camera focus. Validates order fields (weeks/type/target), direction (asc/desc), and campaign type.
- [x] `marketing launch` **[Viewport: Y | Help: Single | Errors: Guided]** — Creates campaigns via GameAction. Requires `--type [...] [--ride-name/--ride-id|--item] --weeks`. Opens marketing window and pans to ride if applicable. Validates campaign type, ride resolution, shop item names, clamps weeks 1-6, and returns GameAction errors if action fails.

## Finance

- [x] `finance status` **[Viewport: Y | Help: Single | Errors: S]** — Cash, park/company value, loan+max, interest rate, operating profit (this/last month), marketing & research spend deltas, and the full expenditure category table (rides, shops, wages, land, construction, etc.). Opens Finance summary window (summary tab), skips camera focus.
- [x] `finance history` **[Viewport: Y | Help: Single | Errors: S]** — Raw histories for cash, weekly profit, and park value series. Opens Finance summary window (graphs tab), skips camera focus.

## Loans

- [x] `loans status` **[Viewport: Y | Help: Single | Errors: S]** — Outstanding balance, cap, cash, park/company value, interest rate, and loan-to-value ratio. Opens Finance summary window (summary tab), skips camera focus.
- [x] `loans set` **[Viewport: Y | Help: Single | Errors: Guided]** — Raises or repays using ParkSetLoanAction (auto-clamped to limits). Requires `--value`. Opens Finance summary window (summary tab), skips camera focus. CLI validates numeric input, RPC returns GameAction errors if action fails.

## Entrances

- [x] `entrances list` **[Viewport: Y | Help: Single | Errors: S]** — Enumerates every park entrance with index, tile coordinates (x, y, z), facing direction, and current park open/closed state.

## Awards

- [x] `awards list` **[Viewport: Y | Help: Single | Errors: S]** — Active awards plus duration countdown. Opens park awards window, pans to entrance.
- [x] `awards history` **[Viewport: S | Help: Single | Errors: Guided]** — Archived award/loss news items with source (recent/archived) and timestamps. Opens recent news window, skips camera focus. Optional `--limit` param with validation (must be >= 1).

## News

- [x] `news list` **[Viewport: S | Help: Single | Errors: Guided]** — Structured feed entries (type/text/day/month/year/ticks), button/subject flags, and source channel. Opens recent news window, skips camera focus. Optional `[--archived true|false --limit]`. Defaults to recent news only. Validates --limit >= 1.
- [x] `news history` **[Viewport: S | Help: Single | Errors: S]** — Opens the Recent Messages window to review notifications and news history. Returns message counts (recent, archived, total).

## Weather & Time

- [x] `weather status` **[Viewport: S | Help: Single | Errors: S]** — Current + next weather type/effect/level, Celsius/F temps, gloom, season/month/day/year, day-progress %, ticks until change, precip flags (rain/snow/storm), umbrella demand heuristic, and freeze-weather cheat state. Opens park information window, skips camera focus.
- [x] `weather forecast` **[Viewport: S | Help: Single | Errors: S]** — Stripped-down next-state payload when only the future matters. Opens park information window, skips camera focus.

## Construction

- [x] `construction land raise` **[Viewport: Y | Help: Single | Errors: Guided]** — Raises flat land by 2 tile units. Requires `--x --y [--size]`. `--x/--y` mark the north-west corner and `--size` applies the change to an N×N square (default 1 tile).
- [x] `construction land lower` **[Viewport: Y | Help: Single | Errors: Guided]** — Lowers flat land by 2 tile units. Requires `--x --y [--size]`. `--x/--y` mark the north-west corner and `--size` applies the change to an N×N square (default 1 tile).
- [x] `construction water raise` **[Viewport: Y | Help: Single | Errors: Guided]** — Raises water level at --x/--y. Use --size for an N×N square brush. Requires `--x --y [--size]`.
- [x] `construction water lower` **[Viewport: Y | Help: Single | Errors: Guided]** — Lowers water level at --x/--y. Use --size for an N×N square brush. Requires `--x --y [--size]`.
- [x] `construction scenery clear` **[Viewport: Y | Help: Single | Errors: Guided]** — Clears scenery from an N×N area. Mirrors the in-game clear scenery tool. At least one filter flag required. Requires `--x --y [--size]` and one or more of `[--small] [--large] [--paths]`. `--small` clears trees, flowers, statues, and walls. `--large` clears multi-tile structures. `--paths` clears footpaths.

## Trees

- [x] `trees catalog` **[Viewport: Y | Help: Single | Errors: S]** — Lists all loaded tree scenery objects with identifiers, prices, and heights. Output includes the exact `--tree-id` value needed for placement.
- [x] `trees place` **[Viewport: Y | Help: Single | Errors: Guided]** — Drop single tree instances pulled from object catalog. Requires `--x --y --tree-id [--z --quadrant]`. CLI validates coordinates are non-negative. Use `trees catalog` to find available tree identifiers.

## Scenery

- [x] `scenery catalog` **[Viewport: Y | Help: Single | Errors: S]** — Lists all loaded small scenery objects (excluding trees) with identifiers, prices, heights, and flags. Output includes the exact `--scenery-id` value needed for placement.
- [x] `scenery place` **[Viewport: Y | Help: Stacked | Errors: Guided]** — Places a scenery item (statue, flower pot, etc.) at the specified tile. Requires `--x --y --scenery-id <object> [--z --quadrant --facing --primary-colour --secondary-colour]`. CLI validates coordinates are non-negative, quadrant is 0-3, and colours are 0-31.

## Path Items

Path items are furniture that goes *on* footpaths: benches for guests to rest, bins for litter, lamps for nighttime lighting, and decorative fountains.

**User-friendly aliases:** Use simple names instead of technical identifiers:
- `bench` → `rct2.footpath_item.bench1`
- `bin` (or `trash`, `trashcan`) → `rct2.footpath_item.litter1`
- `lamp` (or `light`) → `rct2.footpath_item.lamp1`
- `fountain` → `rct2.footpath_item.jumpfnt1`

- [x] `path-items catalog` **[Viewport: Y | Help: Single | Errors: S]** — Lists available path additions with category, price, and usage hints. Use `--category benches|bins|lamps|fountains` to filter. Categories are auto-detected from item flags.
- [x] `path-items place` **[Viewport: Y | Help: Single | Errors: Guided]** — Places a path item on an existing footpath. Requires `--x --y --item-id <alias or identifier> [--z]`. Accepts friendly aliases like `bench`, `bin`, `lamp`. Fails if no path exists at the tile.
- [x] `path-items remove` **[Viewport: Y | Help: Single | Errors: Guided]** — Removes the path item from a footpath tile. Requires `--x --y [--z]`. Only removes the addition, not the path itself.

## Paths

- [x] `paths place` **[Viewport: Y | Help: Single | Errors: Guided]** — Places a footpath or queue tile with auto-slope detection or elevated path support.
  - **Ground paths** (no `--z`): Auto-detects height and slope from terrain. Works on flat or sloped ground; errors on irregular terrain.
  - **Elevated paths** (with `--z`): Places at explicit height, flat by default. Use `--slope north|south|east|west` for ramps.
  - Accepts friendly surface names: `tarmac`, `dirt`, `crazy`, `ash`, `queue_blue`, etc.
  - Accepts friendly railings names: `wood`, `concrete`, `space`, `bamboo`.
  - Requires `--x --y --surface [--railings] [--queue true|false] [--z height] [--slope direction]`.
- [x] `paths catalog` **[Viewport: Y | Help: Single | Errors: S]** — Lists all loaded path surface and railing objects with identifiers for use with `paths place`. Output includes `--surface` and `--railings` values.
- [x] `paths remove` **[Viewport: Y | Help: Single | Errors: Guided]** — Removes a single footpath tile at `--x/--y`. Use `--z` when multiple paths exist at different heights. For bulk removal, use `construction scenery clear --paths`.
- [x] `path-items list` **[Viewport: Y | Help: Single | Errors: Guided]** — Lists items placed ON footpaths (benches, bins, lamps, fountains, queue screens) - NOT the footpaths themselves. For footpath layout, use `map tile` or `map area paths`. Shows type, coordinates, broken/vandalized status, and bin fullness. Use `--type bench|bin|lamp|fountain|queue_screen|all` to filter. Use `--broken` to see only vandalized items. Supports pagination via `--limit` and `--after`.

## Bug Reporting

- [x] `bug report` **[Viewport: S | Help: Single | Errors: Guided]** — Creates a timestamped bug report file in the `bug_reports/` directory. Provide description via `--message <text>` or pipe through stdin.

## Follow Hint Coverage Gaps

These commands are implemented and fully functional, but they currently skip emitting a `followHint`, so follow mode will not auto-pan/retarget after they run:

- `park rating history` — needs a basic park overview intent (likely the park stats page and entrance camera).
- `research status` — should focus the Research window with the queue tab highlighted.
- `marketing status` / `marketing launch` — should bring up the marketing campaigns window (and focus the target ride/shop when applicable).
- `shops catalog` — could open the ride/shop construction browser to align with the textual catalog output.

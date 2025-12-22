# Architecture

## Vision

Run Claude Code inside RollerCoaster Tycoon 2 as an AI agent that manages the park through CLI commands.

This is an experimental fork—a creative hack to explore what happens when you give an AI coding assistant direct control of a simulation game via a purpose-built CLI.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   OpenRCT2 (Fork)                           │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ AI Agent Terminal Window (libvterm + PTY)             │  │
│  │  • Spawns Claude Code automatically                   │  │
│  │  • ANSI color support                                 │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │ spawns                               │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ Claude Code Process                                   │  │
│  │  • Working dir: ai-agent-workspace/                   │  │
│  │  • rctctl in PATH                                     │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │ invokes                              │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ JSON-RPC Server (localhost:9876)                      │  │
│  │  • Exposes game state and actions                     │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │ calls                                │
│  ┌────────────────────▼──────────────────────────────────┐  │
│  │ OpenRCT2 Game APIs                                    │  │
│  │  • Park, Rides, Staff, Guests, Finance, Map, etc.     │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘

External Tool:
┌──────────────┐      ┌──────────────┐
│   rctctl     │─────▶│  JSON-RPC    │
│  (CLI tool)  │      │  (port 9876) │
└──────────────┘      └──────────────┘
```

## Key Decisions

### 1. JSON-RPC 2.0 Protocol

**Why:** Standard, LLM-friendly, simple request/response model

**Protocol:**
- Transport: TCP socket on `127.0.0.1:9876`
- Format: Newline-delimited JSON-RPC 2.0 messages
- Security: Localhost only, no auth (this is a local development toy)

**Implementation:**
- Server: `src/openrct2/scripting/JsonRpcServer.{h,cpp}`
- Handlers: `src/openrct2/scripting/rpc/handlers/` (organized by domain)
- Registry: `src/openrct2/scripting/rpc/HandlerRegistry.{h,cpp}`

### 2. Separate CLI Tool (rctctl)

**Why:** Decouples game from CLI, reusable for scripts/other agents

**Language:** C++ (matches OpenRCT2 ecosystem)

**Design:**
- Synchronous JSON-RPC client
- Human-readable table output (default) + JSON mode (`-o json`)
- Standard CLI patterns: `rctctl <resource> <action> [args] [flags]`

**Structure:**
```
rctctl/
├── src/
│   ├── main.cpp
│   ├── cli/           # Argument parsing
│   ├── commands/      # Command implementations
│   ├── renderers/     # Output formatting (tables, JSON)
│   ├── rpc/           # JSON-RPC client
│   └── util/          # String utilities
└── include/rctctl/    # Headers
```

### 3. Fork OpenRCT2 (Not Plugin)

**Why:** Terminal requires deep integration (PTY, SDL input, rendering pipeline)

**Trade-offs:**
- Full control over window system and process spawning
- Native feel, better performance than plugin approach
- But: diverges from upstream, harder to maintain long-term

### 4. Terminal Window with PTY

**Why:** Real interactive shell experience for Claude Code

**Components:**

1. **PTY Layer** (`src/openrct2/terminal/ShellProcess.{h,cpp}`)
   - macOS/Linux: `forkpty()`
   - Windows: Not implemented (would need ConPTY)
   - Non-blocking I/O

2. **Terminal Emulator** (`src/openrct2/terminal/TerminalSession.{h,cpp}`)
   - libvterm for VT100/xterm ANSI escape sequence parsing
   - 256-color + RGB support
   - Cell-based state management

3. **UI Window** (`src/openrct2-ui/windows/AIAgentTerminal.cpp`)
   - `WindowClass::aiAgentTerminal` (enum value 140)
   - Toolbar button to open
   - Text input routing
   - Resizable window

4. **Session Management** (`src/openrct2/terminal/`)
   - `AIAgentLaunch.{h,cpp}` - Claude Code spawning logic
   - `SessionFileMonitor.{h,cpp}` - Turn detection via session files
   - `SessionLogGenerator.{h,cpp}` - Session logging

**Environment:**
- `TERM=xterm-256color`
- `COLORTERM=truecolor`
- rctctl added to PATH automatically

### 5. Claude Code Auto-Launch

**Why:** Remove friction - terminal opens directly into Claude

**Implementation:**
- Searches PATH for `claude` binary on terminal open
- Spawns with `--dangerously-skip-permissions` (required for non-interactive use)
- Working directory: `ai-agent-workspace/`

**Workspace Setup:**
- `ai-agent-workspace/IN_GAME_AGENT.md` - Agent instructions and rctctl reference
- Auto-prompt rotation for continuous play mode

### 6. Direct Game State Access

**Why:** Reuse existing OpenRCT2 scripting infrastructure

**Method:** `getGameState()` - same mechanism the plugin system uses

**APIs Used:**
- `gameState.park` - Cash, rating, guests, name
- `GetRideManager()` - Ride iteration and status
- Game Actions (e.g., `RideSetStatusAction`) - Mutations with validation

**Pattern:** All mutations go through Game Actions for proper validation and multiplayer support

### 7. Command Structure

**Philosophy:** Unix-like, composable, supports automation

**Format:**
```
rctctl <resource> <action> [args] [flags]

Examples:
  rctctl park status
  rctctl rides list -o json
  rctctl ride open 0
  rctctl staff hire mechanic
  rctctl map tile 10 20
```

**Flags:**
- `-o json` - Machine-readable output
- `--host`, `--port` - Server override
- `--help` - Command help

### 8. API Endpoint Pattern

**Handler Registration:**
1. Handlers registered in `HandlerRegistry` at startup
2. Each domain has dedicated handler file (e.g., `RideHandlers.cpp`, `ParkHandlers.cpp`)
3. Handler accesses `getGameState()` or executes Game Action
4. Returns JSON response

**Error Handling:**
- Invalid method: JSON-RPC error -32601 (method not found)
- Invalid params: JSON-RPC error -32602 (invalid params)
- Game errors: Custom error codes with descriptive messages

### 9. Build System

**OpenRCT2:**
- CMake build system (existing)
- libvterm linked via `find_package()` + `target_link_libraries()`
- Requires libvterm installed (e.g., `brew install libvterm` on macOS)

**rctctl:**
- Separate CMake project under `rctctl/`
- Standalone binary (no game dependencies beyond JSON-RPC)
- POSIX sockets (macOS/Linux only currently)

**Quick Build:**
```bash
cmake --build build --target agent_bundle -j8
```

### 10. Testing Strategy

**Headless Mode:** `openrct2-cli host <park.park> --headless`
- JSON-RPC server starts automatically
- Test with rctctl CLI or Python scripts

**Test Suites:**
- `ctest -R rctctl_validation` - CLI validation (fast, no game needed)
- `ctest -R agent_scenarios` - End-to-end with headless game

**Test Parks:** `test/tests/testdata/parks/*.sv6`

## File Structure

```
├── src/openrct2/
│   ├── scripting/
│   │   ├── JsonRpcServer.{h,cpp}           # TCP server
│   │   ├── ScriptEngine.cpp                # Modified (init server)
│   │   └── rpc/
│   │       ├── HandlerRegistry.{h,cpp}     # Method dispatch
│   │       ├── RpcTypes.h                  # Type definitions
│   │       └── handlers/
│   │           ├── ParkHandlers.cpp
│   │           ├── RideHandlers.cpp
│   │           ├── StaffHandlers.cpp
│   │           ├── GuestHandlers.cpp
│   │           ├── FinanceHandlers.cpp
│   │           ├── MapHandlers.cpp
│   │           └── ...
│   └── terminal/
│       ├── ShellProcess.{h,cpp}            # PTY abstraction
│       ├── TerminalSession.{h,cpp}         # libvterm wrapper
│       ├── AIAgentLaunch.{h,cpp}           # Claude spawning
│       ├── SessionFileMonitor.{h,cpp}      # Turn detection
│       └── SessionLogGenerator.{h,cpp}     # Logging
├── src/openrct2-ui/
│   └── windows/
│       └── AIAgentTerminal.cpp             # Terminal UI
├── rctctl/                                 # CLI tool (separate binary)
│   ├── CMakeLists.txt
│   ├── src/
│   └── include/
└── ai-agent-workspace/                     # Claude's working directory
    └── IN_GAME_AGENT.md                    # Agent instructions
```

## Platform Support

**macOS:** Primary development platform. Works out of the box with Homebrew dependencies.

**Linux:** Should work (uses same POSIX APIs) but less tested.

**Windows:** Not currently supported. Would require:
- ConPTY implementation for terminal
- WinSock conversion for rctctl
- Contributions welcome!

## Limitations & Caveats

This is a hack/experiment, not production software:

- **Local only:** No network play support, localhost RPC only
- **macOS-centric:** Developed and tested primarily on macOS
- **Claude-specific:** Auto-launch assumes Claude Code CLI is installed
- **No sandbox:** `--dangerously-skip-permissions` bypasses Claude's safety prompts
- **Diverged fork:** Will drift from upstream OpenRCT2 over time


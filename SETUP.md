# Setup

Quick guide for getting Claude Code x OpenRCT2 running.

## Prerequisites

- **macOS** (Sonoma or newer) with Xcode command line tools, CMake 3.24+, and Ninja
- **libvterm** for terminal rendering: `brew install libvterm pkg-config`
- **RollerCoaster Tycoon 2** purchased on [Steam](https://store.steampowered.com/app/285330/) or [GOG](https://www.gog.com/game/rollercoaster_tycoon_2). Launch it once so the assets install, then ensure the game files are in `~/Library/Application Support/OpenRCT2/`
- **(Optional)** [Claude Code CLI](https://docs.anthropic.com/en/docs/claude-code) for the full AI agent experience; otherwise a simple bootstrap REPL is provided

Linux should work but is less tested. Windows is not currently supported.

## Clone & Configure

```bash
git clone <repo-url>
cd OpenRCT2
cmake -S . -B build -G Ninja
```

If you need to build without libvterm (e.g., on a platform where it isn't packaged), append `-DOPENRCT2_ALLOW_VTERM_FALLBACK=ON`. ANSI colors will be disabled.

## Build

```bash
cmake --build build --target agent_bundle -j8
```

This compiles OpenRCT2, the terminal UI, `rctctl`, and sprite assets together.

## Launch

```bash
./build/OpenRCT2.app/Contents/MacOS/OpenRCT2
```

Click the toolbar button (robot icon) to open the AI Agent terminal.

## Terminal Launch Priority

When the terminal opens, it uses the first available option:

1. **`AGENT_TERMINAL_COMMAND`** env var — runs whatever command you supply
2. **Claude Code CLI** — if `claude` is in your PATH, runs `claude --dangerously-skip-permissions`
3. **Bootstrap REPL** — falls back to `scripts/agent_bootstrap.sh`, a simple REPL for `rctctl` commands

## Bootstrap REPL

When the fallback script runs, it:
- Creates workspace at `~/.openrct2-agent`
- Verifies `rctctl` exists and exposes it on `PATH`
- Provides a REPL where you type commands like `park status`, `rides list`, etc.

Built-in commands:
- `help` — show help
- `:text` / `:json` — toggle output format
- `:shell` — drop to interactive shell
- `!<cmd>` — run a raw shell command
- `:q` / `quit` / `exit` — exit

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `AGENT_TERMINAL_COMMAND` | — | Override the entire terminal command |
| `AGENT_BYPASS` | `0` | Set to `1` to drop to a plain shell instead of launching Claude/REPL |
| `AGENT_HOST` | `127.0.0.1` | JSON-RPC server host (used by bootstrap REPL) |
| `AGENT_PORT` | `9876` | JSON-RPC server port |
| `AGENT_FORMAT` | `text` | Default output format (`text` or `json`) |

## Troubleshooting

- **Build everything after pulling:** `cmake --build build --target agent_bundle -j8`
- **Workspace location:** `~/.openrct2-agent` — delete to reset agent state
- **Session logs:** Saved to `agent-logs/` in the repo root (see AGENTS.md)
- **Game logs:** Run with `--verbose --log-file game-logs/session.log`

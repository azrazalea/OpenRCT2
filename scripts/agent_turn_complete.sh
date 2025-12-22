#!/bin/bash
# agent_turn_complete.sh - Signal script for Claude Code's Stop hook
#
# This script is called by Claude Code when the agent finishes responding.
# It writes a timestamp to a signal file that the game's SessionFileMonitor watches.
#
# The signal file location is determined by CLAUDE_PROJECT_DIR (set by Claude Code)
# or falls back to the default ai-agent-workspace location.

WORKSPACE_DIR="${CLAUDE_PROJECT_DIR:-$HOME/.openrct2-agent}"
SIGNAL_FILE="$WORKSPACE_DIR/.turn-complete"

# Write Unix timestamp to signal file
# The game will detect this file change and know the turn is complete
echo "$(date +%s)" > "$SIGNAL_FILE"

#!/usr/bin/env bash
# agent_session_wrapper.sh - Wrapper for Claude Code that converts session logs to HTML
#
# This script:
# 1. Runs Claude Code with the provided arguments
# 2. After Claude exits, converts the ~/.claude/projects/ data to a beautiful HTML artifact
# 3. Saves the HTML to agent-logs/ alongside the session metadata
#
# Environment variables:
#   AGENT_SESSION_LOG  - Base path for session logs (without extension)
#   AGENT_WORKSPACE    - The workspace directory Claude is running in
#
# Usage:
#   agent_session_wrapper.sh <claude-binary> [claude-args...]

set -euo pipefail

# Parse arguments
if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <claude-binary> [claude-args...]" >&2
    exit 1
fi

CLAUDE_BIN="$1"
shift

# Get session log path from environment (set by AIAgentLaunch.cpp)
SESSION_LOG_BASE="${AGENT_SESSION_LOG:-}"
WORKSPACE="${AGENT_WORKSPACE:-}"

# Determine repo root for agent-logs directory
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
AGENT_LOGS_DIR="${REPO_ROOT}/agent-logs"

# Create agent-logs directory if needed
mkdir -p "${AGENT_LOGS_DIR}"

# Generate session identifier for filenames
SESSION_ID=$(date +%Y%m%d-%H%M%S)
if [[ -n "${SESSION_LOG_BASE}" ]]; then
    # Extract the filename stem from the provided path
    SESSION_STEM=$(basename "${SESSION_LOG_BASE}" .log)
else
    SESSION_STEM="agent-session-${SESSION_ID}"
fi

# Paths for output files
HTML_OUTPUT="${AGENT_LOGS_DIR}/${SESSION_STEM}.html"
METADATA_OUTPUT="${AGENT_LOGS_DIR}/${SESSION_STEM}.json"

# Track start time for metadata
START_TIME=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
CLAUDE_EXIT_CODE=0
CLEANUP_DONE=false

# Cleanup function that generates HTML - called on exit or signal
generate_html_log() {
    # Prevent running twice
    if [[ "${CLEANUP_DONE}" == "true" ]]; then
        return
    fi
    CLEANUP_DONE=true

    local END_TIME
    END_TIME=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    echo ""
    echo "[agent-wrapper] Claude Code exited with code: ${CLAUDE_EXIT_CODE}"
    echo "[agent-wrapper] Converting session to HTML..."

    # Find the Claude project directory for this workspace
    local CLAUDE_PROJECTS_DIR="${HOME}/.claude/projects"
    local PROJECT_DIR=""
    local HTML_GENERATED="false"

    if [[ -d "${CLAUDE_PROJECTS_DIR}" ]]; then
        # First, try to find based on workspace path
        if [[ -n "${WORKSPACE}" ]]; then
            local WORKSPACE_SLUG
            WORKSPACE_SLUG=$(echo "${WORKSPACE}" | sed 's|^/||' | sed 's|/|-|g')
            PROJECT_DIR="${CLAUDE_PROJECTS_DIR}/-${WORKSPACE_SLUG}"
        fi

        # If that doesn't exist, find the most recently modified project
        if [[ ! -d "${PROJECT_DIR:-}" ]]; then
            PROJECT_DIR=$(find "${CLAUDE_PROJECTS_DIR}" -maxdepth 1 -type d -name "-*" -print0 2>/dev/null | \
                xargs -0 ls -1td 2>/dev/null | head -1 || true)
        fi

        if [[ -n "${PROJECT_DIR}" && -d "${PROJECT_DIR}" ]]; then
            echo "[agent-wrapper] Found Claude project directory: ${PROJECT_DIR}"

            # Find the most recent JSONL file (current session) - not all historical sessions
            local CURRENT_SESSION_JSONL
            CURRENT_SESSION_JSONL=$(find "${PROJECT_DIR}" -maxdepth 1 -name "*.jsonl" -type f -print0 2>/dev/null | \
                xargs -0 ls -1t 2>/dev/null | head -1 || true)

            if [[ -z "${CURRENT_SESSION_JSONL}" ]]; then
                echo "[agent-wrapper] Warning: No JSONL session file found in project directory"
            # Check if uvx is available
            elif command -v uvx >/dev/null 2>&1; then
                echo "[agent-wrapper] Running claude-code-log on: $(basename "${CURRENT_SESSION_JSONL}")"

                # Run claude-code-log on the specific session file, not the entire directory
                if uvx "claude-code-log@0.3.4" -o "${HTML_OUTPUT}" "${CURRENT_SESSION_JSONL}" 2>&1; then
                    echo "[agent-wrapper] HTML session log saved to: ${HTML_OUTPUT}"
                    HTML_GENERATED="true"
                else
                    echo "[agent-wrapper] Warning: claude-code-log@0.3.4 failed, trying latest..."
                    if uvx claude-code-log -o "${HTML_OUTPUT}" "${CURRENT_SESSION_JSONL}" 2>&1; then
                        echo "[agent-wrapper] HTML session log saved to: ${HTML_OUTPUT}"
                        HTML_GENERATED="true"
                    else
                        echo "[agent-wrapper] Warning: Failed to generate HTML log"
                    fi
                fi
            else
                echo "[agent-wrapper] Warning: uvx not found. Install uv to enable HTML log generation."
            fi
        else
            echo "[agent-wrapper] Warning: Could not find Claude project directory"
        fi
    else
        echo "[agent-wrapper] Warning: ~/.claude/projects directory not found"
    fi

    # Update metadata with completion info
    cat > "${METADATA_OUTPUT}" <<EOF
{
  "session_id": "${SESSION_STEM}",
  "start_time": "${START_TIME}",
  "end_time": "${END_TIME}",
  "workspace": "${WORKSPACE}",
  "claude_binary": "${CLAUDE_BIN}",
  "exit_code": ${CLAUDE_EXIT_CODE},
  "html_generated": ${HTML_GENERATED},
  "html_path": "${HTML_OUTPUT}",
  "status": "completed"
}
EOF

    echo "[agent-wrapper] Session metadata saved to: ${METADATA_OUTPUT}"

    if [[ "${HTML_GENERATED}" == "true" ]]; then
        echo ""
        echo "[agent-wrapper] View the session log with:"
        echo "  open ${HTML_OUTPUT}"
    fi
}

# Trap signals to ensure cleanup runs even if killed
trap 'CLAUDE_EXIT_CODE=$?; generate_html_log' EXIT
trap 'CLAUDE_EXIT_CODE=130; exit 130' INT
trap 'CLAUDE_EXIT_CODE=143; exit 143' TERM

echo "[agent-wrapper] Starting Claude Code session"
echo "[agent-wrapper] Session ID: ${SESSION_STEM}"
echo "[agent-wrapper] HTML will be saved to: ${HTML_OUTPUT}"

# Record session start metadata
cat > "${METADATA_OUTPUT}" <<EOF
{
  "session_id": "${SESSION_STEM}",
  "start_time": "${START_TIME}",
  "workspace": "${WORKSPACE}",
  "claude_binary": "${CLAUDE_BIN}",
  "status": "running"
}
EOF

# Run Claude Code
# The EXIT trap will handle HTML generation when this exits
"${CLAUDE_BIN}" "$@"
CLAUDE_EXIT_CODE=$?

# The generate_html_log function will be called automatically via EXIT trap

#!/usr/bin/env bash
set -euo pipefail

if [[ ${AGENT_TRACE:-0} != 0 ]]; then
    set -x
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
DEFAULT_WORKSPACE="$HOME/.openrct2-agent"
AGENT_WORKSPACE=${AGENT_WORKSPACE:-$DEFAULT_WORKSPACE}
HOST=${AGENT_HOST:-${RCTCTL_HOST:-127.0.0.1}}
PORT=${AGENT_PORT:-${RCTCTL_PORT:-9876}}
DEFAULT_FORMAT=${AGENT_FORMAT:-text}

find_rctctl() {
    if command -v rctctl >/dev/null 2>&1; then
        command -v rctctl
        return 0
    fi

    local candidates=(
        "$REPO_ROOT/build/rctctl/rctctl"
        "$REPO_ROOT/build/rctctl/Release/rctctl"
        "$REPO_ROOT/build/rctctl/Debug/rctctl"
        "$REPO_ROOT/build/bin/rctctl"
        "$REPO_ROOT/build/rctctl"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -x "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done

    return 1
}

if [[ ${AGENT_BYPASS:-0} != 0 ]]; then
    echo "[ai-agent] AGENT_BYPASS=1 set—dropping to interactive shell." >&2
    exec /bin/bash --login
fi

if ! RCTCTL_BIN=$(find_rctctl); then
    cat >&2 <<'EOF'
[ai-agent] Unable to locate the rctctl binary.
[ai-agent] Build it with `cmake --build build --target rctctl` (or use the agent_bundle target) before launching the terminal.
[ai-agent] If you deliberately want a raw shell, relaunch OpenRCT2 with AGENT_BYPASS=1. Otherwise, this bootstrap exits to keep the agent constrained to rctctl.
EOF
    exit 90
fi

run_rctctl() {
    local inject_format=1
    for arg in "$@"; do
        case "$arg" in
            -o|--json)
                inject_format=0
                break
                ;;
        esac
    done

    local cmd=("$RCTCTL_BIN" --host "$HOST" --port "$PORT")
    if [[ $inject_format -ne 0 && -n "$DEFAULT_FORMAT" ]]; then
        cmd+=(-o "$DEFAULT_FORMAT")
    fi
    cmd+=("$@")
    "${cmd[@]}"
}

split_command() {
    local line="$1"
    local parsed
    if ! parsed=$(
        python3 - "$line" <<'PY'
import shlex
import sys
line = sys.argv[1]
try:
    print("\n".join(shlex.split(line)))
except ValueError as err:
    sys.stderr.write(f"split error: {err}\n")
    sys.exit(1)
PY
    ); then
        return 1
    fi
    if [[ -z "$parsed" ]]; then
        __AGENT_ARGS=()
    else
        mapfile -t __AGENT_ARGS <<<"$parsed"
    fi
}

print_help() {
    cat <<EOF_HELP
AI Agent Bootstrap
------------------
This shell runs rctctl against the in-game JSON-RPC server.
Workspace: ${AGENT_WORKSPACE}
Host: ${HOST}:${PORT}
Type commands exactly as you would after "rctctl" (e.g. "park status").
Built-ins:
  help            - Show this message
  :text / :json   - Toggle default output format
  :shell          - Drop to an interactive login shell
  :q / quit       - Exit the bootstrap
  !<cmd>          - Run a raw shell command ("!ls", "!cat notes.txt", ...)
EOF_HELP
}

agent_loop() {
    print_help
    while true; do
        printf 'rctctl> '
        if ! IFS= read -r line; then
            printf '\n'
            break
        fi
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        if [[ -z "$line" ]]; then
            continue
        fi
        case "$line" in
            help|:help|?)
                print_help
                continue
                ;;
            :json)
                DEFAULT_FORMAT="json"
                echo "[ai-agent] Default output format set to json"
                continue
                ;;
            :text)
                DEFAULT_FORMAT="text"
                echo "[ai-agent] Default output format set to text"
                continue
                ;;
            :shell)
                exec /bin/bash --login
                ;;
            :q|quit|exit)
                break
                ;;
            !*)
                local shell_cmd=${line:1}
                if [[ -z "$shell_cmd" ]]; then
                    continue
                fi
                bash -lc "$shell_cmd"
                continue
                ;;
            \#*)
                continue
                ;;
        esac

        if ! split_command "$line"; then
            echo "[ai-agent] Unable to parse command" >&2
            continue
        fi

        if [[ ${#__AGENT_ARGS[@]} -eq 0 ]]; then
            continue
        fi

        if ! run_rctctl "${__AGENT_ARGS[@]}"; then
            status=$?
            echo "[ai-agent] rctctl exited with status $status" >&2
        fi
    done
}

if (($# > 0)); then
    run_rctctl "$@"
    exit $?
fi

agent_loop

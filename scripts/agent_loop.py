#!/usr/bin/env python3
"""
agent_loop.py - External orchestrator for continuous Claude agent operation

This script manages the continuous operation of the in-game Claude agent by:
1. Polling agent.status for turn completion via JSON-RPC
2. Sending the next prompt from a rotation when Claude is ready
3. Auto-restarting if Claude crashes

Usage:
    python3 agent_loop.py [--prompts FILE] [--host HOST] [--port PORT]

Prerequisites:
- OpenRCT2 running with JSON-RPC server enabled (:9876)
- AI Agent terminal window open with autoplay enabled
"""

import argparse
import json
import socket
import sys
import time
from pathlib import Path

# Default configuration
DEFAULT_PROMPTS_FILE = Path("~/.openrct2-agent/prompts.txt").expanduser()
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 9876
POLL_INTERVAL = 2  # seconds between status checks


def rpc_call(host: str, port: int, method: str, params: dict = None) -> dict:
    """Send a JSON-RPC request to the game and return the result."""
    request = {
        "jsonrpc": "2.0",
        "method": method,
        "id": 1,
    }
    if params:
        request["params"] = params

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(10)
            sock.connect((host, port))

            # Send request
            request_bytes = json.dumps(request).encode('utf-8') + b'\n'
            sock.sendall(request_bytes)

            # Receive response
            response_data = b''
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response_data += chunk
                if b'\n' in response_data:
                    break

            response = json.loads(response_data.decode('utf-8').strip())
            if 'error' in response:
                print(f"RPC error: {response['error']}", file=sys.stderr)
                return None
            return response.get('result')
    except (socket.error, json.JSONDecodeError, ConnectionRefusedError) as e:
        print(f"RPC connection error: {e}", file=sys.stderr)
        return None


def get_agent_status(host: str, port: int) -> dict:
    """Get the current agent status."""
    return rpc_call(host, port, "agent.status")


def send_prompt(host: str, port: int, text: str) -> bool:
    """Send a prompt to the agent."""
    result = rpc_call(host, port, "agent.sendPrompt", {"text": text})
    return result and result.get("success", False)


def restart_agent(host: str, port: int) -> bool:
    """Restart the agent terminal."""
    result = rpc_call(host, port, "agent.restart")
    return result and result.get("success", False)


def load_prompts(prompts_file: Path) -> list:
    """Load prompts from file, one per line."""
    if not prompts_file.exists():
        print(f"Prompts file not found: {prompts_file}", file=sys.stderr)
        return [
            "Check the park status and address any urgent issues.",
            "Review guest feedback and make improvements.",
        ]

    with open(prompts_file, 'r') as f:
        prompts = [line.strip() for line in f if line.strip() and not line.startswith('#')]

    if not prompts:
        print(f"No prompts found in {prompts_file}", file=sys.stderr)
        return ["Check the park status."]

    return prompts


def wait_for_turn_complete(host: str, port: int) -> str:
    """
    Wait for Claude to complete its turn by polling agent.status.

    Returns:
        "ready" - Claude finished its turn, ready for next prompt
        "exited" - agent process exited
        "not_running" - agent terminal not running
        "error" - connection error
    """
    last_turn_ts = 0

    while True:
        status = get_agent_status(host, port)
        if status is None:
            print("Lost connection to game")
            return "error"

        agent_status = status.get("status", "not_running")

        if agent_status == "exited":
            print("Agent process exited")
            return "exited"

        if agent_status == "not_running":
            print("Agent terminal not running")
            return "not_running"

        # Check if turn is complete (from SessionFileMonitor)
        turn_complete = status.get("turnComplete", False)
        turn_ts = status.get("lastTurnCompleteTimestamp", 0)

        if turn_complete and turn_ts > last_turn_ts:
            print(f"Turn complete detected (timestamp: {turn_ts})")
            return "ready"

        # Update last seen timestamp to avoid re-triggering on same completion
        if turn_ts > last_turn_ts:
            last_turn_ts = turn_ts

        time.sleep(POLL_INTERVAL)


def main():
    parser = argparse.ArgumentParser(description="Orchestrate continuous Claude agent operation")
    parser.add_argument("--prompts", type=Path, default=DEFAULT_PROMPTS_FILE,
                        help="Path to prompts file")
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help="JSON-RPC server host")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="JSON-RPC server port")
    parser.add_argument("--once", action="store_true",
                        help="Send one prompt and exit (for testing)")
    args = parser.parse_args()

    print(f"Loading prompts from: {args.prompts}")
    prompts = load_prompts(args.prompts)
    print(f"Loaded {len(prompts)} prompts")

    prompt_index = 0

    try:
        print(f"Connecting to game at {args.host}:{args.port}")

        # Verify connection
        status = get_agent_status(args.host, args.port)
        if status is None:
            print("Cannot connect to game. Is OpenRCT2 running with JSON-RPC enabled?")
            sys.exit(1)

        print(f"Initial agent status: {status.get('status', 'unknown')}")
        print("Note: Enable autoplay in the AI Agent terminal for turn detection to work")

        while True:
            print(f"\n--- Waiting for Claude to finish turn ---")

            result = wait_for_turn_complete(args.host, args.port)

            if result == "exited":
                print("Attempting to restart agent...")
                if restart_agent(args.host, args.port):
                    print("Agent restarted successfully")
                    time.sleep(5)  # Give Claude time to initialize
                else:
                    print("Failed to restart agent. Waiting...")
                    time.sleep(10)
                continue

            if result == "not_running":
                print("Agent terminal not open. Waiting...")
                time.sleep(10)
                continue

            if result == "error":
                print("Connection error. Retrying in 10 seconds...")
                time.sleep(10)
                continue

            # Ready to send next prompt
            prompt = prompts[prompt_index % len(prompts)]
            prompt_index += 1

            print(f"\nSending prompt {prompt_index}: {prompt[:60]}...")

            if send_prompt(args.host, args.port, prompt):
                print("Prompt sent successfully")
            else:
                print("Failed to send prompt")

            if args.once:
                print("--once flag set, exiting")
                break

            # Brief pause before checking for ready again
            time.sleep(3)

    except KeyboardInterrupt:
        print("\nInterrupted by user")


if __name__ == "__main__":
    main()

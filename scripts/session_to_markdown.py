#!/usr/bin/env python3
"""
session_to_markdown.py - Convert Claude Code JSONL sessions to clean Markdown

Produces a dense, LLM-friendly transcript without HTML/CSS bloat.
Can process individual JSONL files or entire project directories.

Usage:
    session_to_markdown.py <input> [-o output.md] [--include-thinking]

Examples:
    # Convert specific session file
    ./session_to_markdown.py ~/.claude/projects/-Users.../abc123.jsonl

    # Convert most recent session from a project directory
    ./session_to_markdown.py ~/.claude/projects/-Users-foo-workspace/

    # Include thinking blocks (hidden by default for density)
    ./session_to_markdown.py session.jsonl --include-thinking
"""

import argparse
import json
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional


def find_most_recent_jsonl(project_dir: Path) -> Optional[Path]:
    """Find the most recently modified .jsonl file in a directory."""
    jsonl_files = list(project_dir.glob("*.jsonl"))
    if not jsonl_files:
        return None
    # Sort by modification time, newest first
    jsonl_files.sort(key=lambda f: f.stat().st_mtime, reverse=True)
    return jsonl_files[0]


def find_claude_project_dir(workspace_path: str) -> Optional[Path]:
    """Convert workspace path to Claude's project directory path."""
    home = os.environ.get("HOME", "")
    if not home:
        return None

    projects_dir = Path(home) / ".claude" / "projects"
    if not projects_dir.exists():
        return None

    # Convert workspace path to project dir name
    # e.g., /Users/foo/Library/Application Support/OpenRCT2/ai-agent-workspace
    # becomes -Users-foo-Library-Application-Support-OpenRCT2-ai-agent-workspace
    workspace_slug = workspace_path.lstrip("/").replace("/", "-").replace(" ", "-")
    project_dir = projects_dir / f"-{workspace_slug}"

    if project_dir.exists():
        return project_dir

    # Fall back to most recently modified project
    candidates = [d for d in projects_dir.iterdir() if d.is_dir() and d.name.startswith("-")]
    if candidates:
        candidates.sort(key=lambda d: d.stat().st_mtime, reverse=True)
        return candidates[0]

    return None


def parse_timestamp(ts: str) -> Optional[datetime]:
    """Parse ISO timestamp string."""
    try:
        # Handle various formats
        ts = ts.replace("Z", "+00:00")
        return datetime.fromisoformat(ts)
    except:
        return None


def format_tool_call(tool_name: str, tool_input: dict) -> str:
    """Format a tool call for markdown output."""
    lines = [f"**Tool: {tool_name}**"]

    if tool_name == "Bash":
        cmd = tool_input.get("command", "")
        desc = tool_input.get("description", "")
        if desc:
            lines.append(f"_{desc}_")
        lines.append("```bash")
        lines.append(cmd)
        lines.append("```")
    elif tool_name == "Read":
        path = tool_input.get("file_path", "")
        lines.append(f"Reading: `{path}`")
    elif tool_name == "Write":
        path = tool_input.get("file_path", "")
        content = tool_input.get("content", "")
        lines.append(f"Writing: `{path}`")
        if len(content) < 500:
            lines.append("```")
            lines.append(content)
            lines.append("```")
        else:
            lines.append(f"_({len(content)} characters)_")
    elif tool_name == "Edit":
        path = tool_input.get("file_path", "")
        old = tool_input.get("old_string", "")
        new = tool_input.get("new_string", "")
        lines.append(f"Editing: `{path}`")
        lines.append("```diff")
        for line in old.split("\n")[:10]:
            lines.append(f"- {line}")
        if old.count("\n") > 10:
            lines.append(f"... ({old.count(chr(10)) - 10} more lines)")
        for line in new.split("\n")[:10]:
            lines.append(f"+ {line}")
        if new.count("\n") > 10:
            lines.append(f"... ({new.count(chr(10)) - 10} more lines)")
        lines.append("```")
    elif tool_name in ("Grep", "Glob"):
        pattern = tool_input.get("pattern", "")
        path = tool_input.get("path", ".")
        lines.append(f"Pattern: `{pattern}` in `{path}`")
    else:
        # Generic tool
        if tool_input:
            lines.append("```json")
            lines.append(json.dumps(tool_input, indent=2)[:500])
            lines.append("```")

    return "\n".join(lines)


def format_tool_result(content: str, is_error: bool = False) -> str:
    """Format a tool result for markdown output."""
    prefix = "**Error:**" if is_error else "**Output:**"

    # Truncate very long outputs
    if len(content) > 2000:
        content = content[:2000] + f"\n... (truncated, {len(content) - 2000} more chars)"

    return f"{prefix}\n```\n{content}\n```"


def convert_session_to_markdown(
    jsonl_path: Path,
    include_thinking: bool = False,
    include_timestamps: bool = False
) -> str:
    """Convert a JSONL session file to markdown."""

    lines = []
    lines.append(f"# Session: {jsonl_path.stem}")
    lines.append("")

    session_info = {}
    message_count = 0

    with open(jsonl_path, "r") as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue

            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue

            entry_type = entry.get("type", "")

            # Skip metadata entries
            if entry_type in ("file-history-snapshot", "summary"):
                continue

            # Extract session info from first message
            if not session_info and entry.get("sessionId"):
                session_info = {
                    "session_id": entry.get("sessionId", ""),
                    "workspace": entry.get("cwd", ""),
                    "version": entry.get("version", ""),
                }

            timestamp = entry.get("timestamp", "")
            ts_str = ""
            if include_timestamps and timestamp:
                dt = parse_timestamp(timestamp)
                if dt:
                    ts_str = f" _{dt.strftime('%H:%M:%S')}_"

            message = entry.get("message", {})
            role = message.get("role", "")
            content = message.get("content", "")

            # User message
            if entry_type == "user" and role == "user":
                # Check if this is a tool result or actual user message
                if isinstance(content, str):
                    message_count += 1
                    lines.append(f"## User{ts_str}")
                    lines.append("")
                    lines.append(content.strip())
                    lines.append("")
                elif isinstance(content, list):
                    # Tool results
                    for item in content:
                        if item.get("type") == "tool_result":
                            result_content = item.get("content", "")
                            is_error = item.get("is_error", False)
                            lines.append(format_tool_result(result_content, is_error))
                            lines.append("")

            # Assistant message
            elif entry_type == "assistant" and role == "assistant":
                if isinstance(content, list):
                    for item in content:
                        item_type = item.get("type", "")

                        if item_type == "thinking" and include_thinking:
                            thinking = item.get("thinking", "")
                            if thinking:
                                lines.append("<details><summary>Thinking</summary>")
                                lines.append("")
                                lines.append(thinking.strip())
                                lines.append("")
                                lines.append("</details>")
                                lines.append("")

                        elif item_type == "text":
                            text = item.get("text", "")
                            if text.strip():
                                # Only add header for first text block after user message
                                if not any(l.startswith("## Assistant") for l in lines[-5:]):
                                    lines.append(f"## Assistant{ts_str}")
                                    lines.append("")
                                lines.append(text.strip())
                                lines.append("")

                        elif item_type == "tool_use":
                            tool_name = item.get("name", "unknown")
                            tool_input = item.get("input", {})
                            lines.append(format_tool_call(tool_name, tool_input))
                            lines.append("")

    # Add session info header
    if session_info:
        header_lines = [
            f"**Workspace:** `{session_info.get('workspace', 'N/A')}`",
            f"**Claude Code Version:** {session_info.get('version', 'N/A')}",
            f"**Messages:** {message_count}",
            "",
            "---",
            "",
        ]
        lines = lines[:2] + header_lines + lines[2:]

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Convert Claude Code JSONL sessions to clean Markdown"
    )
    parser.add_argument(
        "input",
        help="Path to JSONL file, project directory, or workspace path"
    )
    parser.add_argument(
        "-o", "--output",
        help="Output markdown file (default: stdout or input.md)"
    )
    parser.add_argument(
        "--include-thinking",
        action="store_true",
        help="Include thinking/reasoning blocks (collapsed)"
    )
    parser.add_argument(
        "--timestamps",
        action="store_true",
        help="Include timestamps on messages"
    )
    parser.add_argument(
        "--workspace",
        help="Workspace path to find Claude project directory"
    )

    args = parser.parse_args()

    input_path = Path(args.input)

    # Determine the JSONL file to process
    jsonl_path = None

    if input_path.is_file() and input_path.suffix == ".jsonl":
        jsonl_path = input_path
    elif input_path.is_dir():
        # Look for most recent JSONL in directory
        jsonl_path = find_most_recent_jsonl(input_path)
        if not jsonl_path:
            print(f"Error: No .jsonl files found in {input_path}", file=sys.stderr)
            sys.exit(1)
    elif args.workspace:
        # Find project dir from workspace path
        project_dir = find_claude_project_dir(args.workspace)
        if project_dir:
            jsonl_path = find_most_recent_jsonl(project_dir)

    if not jsonl_path or not jsonl_path.exists():
        print(f"Error: Could not find JSONL file from input: {args.input}", file=sys.stderr)
        sys.exit(1)

    # Convert to markdown
    markdown = convert_session_to_markdown(
        jsonl_path,
        include_thinking=args.include_thinking,
        include_timestamps=args.timestamps
    )

    # Output
    if args.output:
        output_path = Path(args.output)
        output_path.write_text(markdown)
        print(f"Written to: {output_path}", file=sys.stderr)
    else:
        print(markdown)


if __name__ == "__main__":
    main()

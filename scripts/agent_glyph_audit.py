#!/usr/bin/env python3
"""Summarise glyph discoveries captured by AIAgentGlyphLogger."""

from __future__ import annotations

import argparse
import json
import sys
from collections import OrderedDict
from pathlib import Path
from typing import Iterable


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "log",
        nargs="?",
        default="agent-glyph-log.ndjson",
        help="Path to the NDJSON log emitted by the game (default: %(default)s)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="Only display the first N glyphs after sorting",
    )
    parser.add_argument(
        "--reverse",
        action="store_true",
        help="Reverse the sort order (descending codepoints)",
    )
    return parser.parse_args()


def load_events(path: Path) -> list[dict]:
    events: list[dict] = []
    if not path.exists():
        raise FileNotFoundError(f"Log file not found: {path}")

    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                print(f"warning: skipping invalid JSON line: {exc}", file=sys.stderr)
                continue
            if event.get("event") != "glyph":
                continue
            events.append(event)
    return events


def sort_key(event: dict) -> int:
    tag = event.get("codepoint", "")
    if isinstance(tag, str) and tag.startswith("U+"):
        try:
            return int(tag[2:], 16)
        except ValueError:
            pass
    return 0


def format_row(event: dict) -> str:
    tag = event.get("codepoint", "?")
    glyph = event.get("char", "") or "?"
    timestamp = event.get("ts", "?")
    return f"{tag:<8} {glyph}  first_seen={timestamp}"


def main() -> int:
    args = parse_args()
    events = load_events(Path(args.log))
    if not events:
        print("No glyph events found.")
        return 0

    events.sort(key=sort_key, reverse=args.reverse)

    limit = args.limit if args.limit and args.limit > 0 else len(events)
    print(f"Loaded {len(events)} unique glyph events from {args.log}")
    print("Showing", min(limit, len(events)), "entries:\n")

    for event in events[:limit]:
        print(format_row(event))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

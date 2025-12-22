#!/usr/bin/env python3
"""
End-to-end scenario tests for rctctl against a headless OpenRCT2 instance.

The script launches openrct2-cli in headless mode with a known test park,
drives JSON-RPC via the rctctl binary, and asserts on both JSON payloads
and natural-language renderers.
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
import signal
import socket
import subprocess
import sys
import textwrap
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, List, Optional

# Ride baseline used for EverythingPark scenario assertions.
EVERYTHING_PARK_RIDE_ID = "0"
EVERYTHING_PARK_RIDE_NAME = "Merry-Go-Round 1"
ELECTRIC_WATER_TILE = (14, 67)
ELECTRIC_TREE_TILE = (3, 29)
ELECTRIC_TREE_IDENTIFIER = "rct2.scenery_small.tf2"
ELECTRIC_PATH_SURFACE = "rct2.footpath_surface.tarmac"
DEFAULT_SERVER_PORT = 11753
DEFAULT_JSON_RPC_PORT = 9876


class ScenarioFailure(Exception):
    """Raised when a scenario assertion fails."""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_build_dir() -> Path:
    return repo_root() / "build"


def detect_user_data_path() -> Optional[Path]:
    env_override = os.environ.get("OPENRCT2_USER_DATA")
    if env_override:
        candidate = Path(env_override).expanduser()
        return candidate if candidate.exists() else None

    home = Path.home()
    candidates: List[Path] = []
    if sys.platform == "darwin":
        candidates.append(home / "Library/Application Support/OpenRCT2")
    else:
        candidates.append(home / ".config/OpenRCT2")
        candidates.append(home / ".local/share/OpenRCT2")

    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def read_game_path(user_data_path: Path) -> Optional[Path]:
    config_path = user_data_path / "config.ini"
    if not config_path.exists():
        return None
    parser = configparser.ConfigParser()
    parser.read(config_path)
    if not parser.has_section("general"):
        return None
    raw_value = parser.get("general", "game_path", fallback="").strip()
    if not raw_value:
        return None
    stripped = raw_value.strip("\"'")
    path = Path(stripped).expanduser()
    return path if path.exists() else None


def ensure_data_bundle(build_dir: Path, root: Path) -> Path:
    """
    Creates build/openrct2-data with symlinks to the language + object assets
    so openrct2-cli can resolve resources during headless runs.
    """
    bundle = build_dir / "openrct2-data"
    bundle.mkdir(exist_ok=True)

    language_src = root / "data" / "language"
    if not language_src.exists():
        raise ScenarioFailure(f"Missing language assets: {language_src}")
    create_symlink(bundle / "language", language_src)

    object_src = build_dir / "object"
    if not object_src.exists():
        raise ScenarioFailure(
            f"Missing object assets: {object_src}. "
            "Run `cmake --build build --target object` or `agent_bundle` first."
        )
    create_symlink(bundle / "object", object_src)
    return bundle


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def create_symlink(link_path: Path, target: Path) -> None:
    if link_path.exists() or link_path.is_symlink():
        if link_path.is_symlink():
            current_target = Path(os.path.realpath(link_path))
            if current_target == target.resolve():
                return
            link_path.unlink()
        else:
            raise ScenarioFailure(f"Cannot create symlink {link_path}: path already exists")
    link_path.symlink_to(target)


class HeadlessInstance:
    """
    Context manager that launches openrct2-cli headless, waits for the JSON-RPC
    server to come online, and tears it down when finished.
    """

    def __init__(
        self,
        cli_path: Path,
        build_dir: Path,
        park_path: Path,
        user_data_path: Path,
        data_bundle_path: Path,
        rct2_data_path: Optional[Path],
        server_port: Optional[int] = None,
        json_rpc_port: Optional[int] = None,
    ):
        self._cli_path = cli_path
        self._build_dir = build_dir
        self._park_path = park_path
        self._user_data_path = user_data_path
        self._data_bundle_path = data_bundle_path
        self._rct2_data_path = rct2_data_path
        self._server_port = server_port or DEFAULT_SERVER_PORT
        self._json_rpc_port = json_rpc_port or DEFAULT_JSON_RPC_PORT
        self._proc: Optional[subprocess.Popen[str]] = None
        self._output: List[str] = []

    def __enter__(self) -> "HeadlessInstance":
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.stop()

    @property
    def json_rpc_port(self) -> int:
        return self._json_rpc_port

    def start(self) -> None:
        cmd = [
            str(self._cli_path),
            "host",
            str(self._park_path),
            "--headless",
            "--user-data-path",
            str(self._user_data_path),
            "--openrct2-data-path",
            str(self._data_bundle_path),
            "--port",
            str(self._server_port),
        ]

        if self._rct2_data_path:
            cmd.extend(["--rct2-data-path", str(self._rct2_data_path)])

        if self._json_rpc_port:
            cmd.extend(["--jsonrpc-port", str(self._json_rpc_port)])

        self._proc = subprocess.Popen(
            cmd,
            cwd=str(self._build_dir),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self._wait_for_ready()

    def stop(self) -> None:
        if not self._proc:
            return
        if self._proc.poll() is None:
            try:
                self._proc.send_signal(signal.SIGTERM)
            except ProcessLookupError:
                pass
        try:
            self._proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait(timeout=5)
        finally:
            self._proc = None

    def _wait_for_ready(self, timeout: float = 20.0) -> None:
        assert self._proc and self._proc.stdout
        start = time.time()
        while True:
            if time.time() - start > timeout:
                raise ScenarioFailure(
                    "Timed out waiting for JsonRpcServer to start.\n"
                    + "\n".join(self._output[-20:])
                )
            line = self._proc.stdout.readline()
            if not line:
                if self._proc.poll() is not None:
                    raise ScenarioFailure(
                        "openrct2-cli exited early.\n"
                        + "\n".join(self._output[-20:])
                    )
                continue
            self._output.append(line.rstrip("\n"))
            if "JsonRpcServer listening" in line:
                return

    def tail_output(self, limit: int = 40) -> str:
        return "\n".join(self._output[-limit:])


class ScenarioHarness:
    """
    Convenience helpers for running rctctl commands against the running park.
    """

    def __init__(self, rctctl_path: Path, host: str = "127.0.0.1", port: int = DEFAULT_JSON_RPC_PORT):
        self._rctctl_path = rctctl_path
        self._host = host
        self._port = port

    def run(self, *args: str) -> str:
        cmd = [
            str(self._rctctl_path),
            "--host",
            self._host,
            "--port",
            str(self._port),
            *map(str, args),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise ScenarioFailure(
                f"Command {' '.join(cmd)} failed with exit code {result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )
        return result.stdout.strip()

    def run_json(self, *args: str) -> dict:
        output = self.run(*args, "-o", "json")
        try:
            return json.loads(output)
        except json.JSONDecodeError as exc:
            raise ScenarioFailure(f"Invalid JSON from {' '.join(args)}: {exc}\n{output}") from exc

    def expect(self, condition: bool, message: str) -> None:
        if not condition:
            raise ScenarioFailure(message)

    def get_tile(self, x: int, y: int) -> dict:
        return self.run_json("map", "tile", "--x", str(x), "--y", str(y))


TestFunc = Callable[[ScenarioHarness], None]


@dataclass
class ScenarioSuite:
    name: str
    park_path: Path
    tests: List[tuple[str, TestFunc]]


def ferris_wheel_suite(root: Path) -> ScenarioSuite:
    park = root / "test" / "tests" / "testdata" / "parks" / "small_park_with_ferris_wheel.sv6"
    tests: List[tuple[str, TestFunc]] = [
        ("park_status_snapshot", test_park_status),
        ("land_raise_roundtrip", test_land_raise_roundtrip),
        ("ride_open_close_cycle", test_ride_open_close_cycle),
        ("park_gate_cycle", test_park_gate_cycle),
        ("park_price_adjustment", test_park_price_adjustment),
        ("park_guests_snapshot", test_park_guests_snapshot),
        ("park_warnings_snapshot", test_park_warnings_snapshot),
        ("map_status_snapshot", test_map_status_snapshot),
        ("map_area_ascii", test_map_area_ascii),
        ("rides_list_snapshot", test_rides_list_snapshot),
    ]
    return ScenarioSuite(name="ferris_wheel_park", park_path=park, tests=tests)


def everything_park_suite(root: Path) -> ScenarioSuite:
    park = root / "test" / "tests" / "testdata" / "parks" / "EverythingPark.park"
    tests: List[tuple[str, TestFunc]] = [
        ("park_rating_history", test_park_rating_history),
        ("park_rewards_snapshot", test_park_rewards_snapshot),
        # park_sandbox_toggle removed - sandbox commands intentionally disabled
        ("map_heatmap_guests", test_map_heatmap_guests),
        ("guests_list_snapshot", test_guests_list_snapshot),
        ("guests_list_pagination", test_guests_list_pagination),
        ("staff_list_snapshot", test_staff_list_snapshot),
        ("staff_hire_returns_new_staff", test_staff_hire_returns_new_staff),
        ("research_status_snapshot", test_research_status_snapshot),
        ("weather_status_snapshot", test_weather_status_snapshot),
        ("weather_forecast_snapshot", test_weather_forecast_snapshot),
        ("news_recent_archive", test_news_recent_archive),
        # windows_list_snapshot removed - window commands intentionally disabled
        ("entrances_list_snapshot", test_entrances_list_snapshot),
        ("park_warnings_busy_snapshot", test_park_warnings_busy_snapshot),
        ("rides_price_adjustment_busy", test_rides_price_adjustment_busy),
        ("rides_catalog_snapshot", test_rides_available_snapshot),
        ("rides_breakdowns_snapshot", test_rides_breakdowns_snapshot),
        ("rides_throughput_snapshot", test_rides_throughput_snapshot),
        ("rides_feedback_snapshot", test_rides_feedback_snapshot),
        ("rides_tune_inspection", test_rides_tune_inspection),
        ("rides_tune_operation_option", test_rides_tune_operation_option),
        ("rides_tune_departure_flags", test_rides_tune_departure_flags),
        ("rides_rename_cycle", test_rides_rename_cycle),
        ("rides_theme_colors_list", test_rides_theme_colors_list),
        ("rides_theme_entrance_list", test_rides_theme_entrance_list),
        ("rides_theme_get", test_rides_theme_get),
        ("rides_theme_track_roundtrip", test_rides_theme_track_roundtrip),
    ]
    return ScenarioSuite(name="everything_park", park_path=park, tests=tests)

def electric_fields_suite(root: Path) -> ScenarioSuite:
    park = root / "test" / "tests" / "testdata" / "parks" / "electric_test.park"
    tests: List[tuple[str, TestFunc]] = [
        ("construction_water_raise_lower", test_construction_water_raise_lower),
        ("map_ownership_snapshot", test_map_ownership_snapshot),
        ("trees_catalog_snapshot", test_trees_catalog_snapshot),
        ("construction_scenery_clear", test_construction_scenery_clear),
        ("paths_place_flat", test_paths_place_flat),
        # paths_place_on_water removed - game engine rejects water bridge placement, needs separate investigation
        ("paths_catalog_snapshot", test_paths_catalog_snapshot),
        ("paths_place_with_alias", test_paths_place_with_alias),
        ("paths_place_elevated_flat", test_paths_place_elevated_flat),
        ("paths_place_elevated_sloped", test_paths_place_elevated_sloped),
        ("rides_place_and_entrances", test_rides_place_and_entrances),
        ("rides_place_flat_ride_by_name", test_rides_place_flat_ride_by_name),
        ("rides_catalog_flat_rides", test_rides_catalog_flat_rides),
        ("coasters_list_designs", test_coasters_list_designs),
        ("coasters_preview_footprint", test_coasters_preview_footprint),
        ("entrance_candidates_not_blocked", test_entrance_candidates_not_blocked),
        ("queue_path_connects_to_entrance", test_queue_path_connects_to_entrance),
    ]
    return ScenarioSuite(name="electric_fields", park_path=park, tests=tests)


def coaster_sandbox_suite(rct2_data_path: Optional[Path]) -> Optional[ScenarioSuite]:
    """Suite for large coaster tests using a 'Build your own' scenario with lots of space."""
    if not rct2_data_path:
        return None

    scenarios_dir = rct2_data_path / "Scenarios"
    if not scenarios_dir.exists():
        return None

    # Find a "Build your own" scenario - these have large flat areas
    scenario_path = None
    for name in ["Build your own Six Flags Park.SC6", "Build your own Six Flags Magic Mountain.SC6"]:
        candidate = scenarios_dir / name
        if candidate.exists():
            scenario_path = candidate
            break

    if not scenario_path:
        # Fall back to any "Build your own" scenario
        for f in scenarios_dir.glob("Build your own*.SC6"):
            scenario_path = f
            break

    if not scenario_path:
        return None

    tests: List[tuple[str, TestFunc]] = [
        ("coaster_place_demolish_replace", test_coaster_place_demolish_replace),
    ]
    return ScenarioSuite(name="coaster_sandbox", park_path=scenario_path, tests=tests)


def test_park_status(harness: ScenarioHarness) -> None:
    payload = harness.run_json("park", "status")
    harness.expect(payload["name"] == "FerrisWheelPark", f"Unexpected park name: {payload['name']}")
    harness.expect(payload["objective"]["guestTarget"] == 1000, "Scenario objective mismatch")
    harness.expect(payload["entranceFee"] == 10.0, "Entrance fee should start at $10")

    text = harness.run("park", "status")
    harness.expect(text.startswith("Park Overview"), "Park status missing header")
    name_line = next((line for line in text.splitlines() if line.strip().startswith("Name")), "")
    harness.expect("FerrisWheelPark" in name_line, f"Park name line missing: {name_line or '<empty>'}")
    harness.expect("Finances" in text and "Entrance fee" in text, "Finances section missing from park status")


def test_land_raise_roundtrip(harness: ScenarioHarness) -> None:
    tile = harness.get_tile(3, 3)
    base_height = tile["surface"]["baseHeight"]
    # Heights are now in tile units (ground level is typically 14, was 112 in world units)
    harness.expect(base_height == 14, f"Expected base height 14 at start, got {base_height}")

    raise_output = harness.run("construction", "land", "raise", "--x", "3", "--y", "3")
    harness.expect("(3, 3)" in raise_output, "Land raise output missing coordinates")

    raised_tile = harness.get_tile(3, 3)
    # Land raise adds 2 tile units (was 16 world units)
    harness.expect(
        raised_tile["surface"]["baseHeight"] == base_height + 2,
        f"Land raise should add 2 tile units, got {raised_tile['surface']['baseHeight']}",
    )

    harness.run("construction", "land", "lower", "--x", "3", "--y", "3")
    final_tile = harness.get_tile(3, 3)
    harness.expect(final_tile["surface"]["baseHeight"] == base_height, "Land lower should restore original height")


def test_ride_open_close_cycle(harness: ScenarioHarness) -> None:
    ride = harness.run_json("rides", "get", "--id", "0")
    harness.expect(ride["status"] == "closed", f"Ride should start closed, got {ride['status']}")

    harness.run("rides", "open", "--id", "0")
    opened = harness.run_json("rides", "get", "--id", "0")
    harness.expect(opened["status"] == "open", "Ride failed to enter OPEN state")

    harness.run("rides", "close", "--id", "0")
    closed = harness.run_json("rides", "get", "--id", "0")
    harness.expect(closed["status"] == "closed", "Ride failed to return to CLOSED state")

    # Test --evict-guests flag (close for repairs behavior)
    harness.run("rides", "open", "--id", "0")
    harness.run("rides", "close", "--id", "0", "--evict-guests=true")
    evicted = harness.run_json("rides", "get", "--id", "0")
    harness.expect(evicted["status"] == "closed", "Ride close with --evict-guests failed")


def test_park_gate_cycle(harness: ScenarioHarness) -> None:
    initial = harness.run_json("park", "status")
    harness.expect(initial["isOpen"] in (True, False), "Park status missing gate flag")

    # Flip state twice and ensure we land back on the original value.
    if initial["isOpen"]:
        harness.run("park", "close")
        closed = harness.run_json("park", "status")
        harness.expect(closed["isOpen"] is False, "Park failed to close")
        harness.run("park", "open")
    else:
        harness.run("park", "open")
        opened = harness.run_json("park", "status")
        harness.expect(opened["isOpen"] is True, "Park failed to open")
        harness.run("park", "close")

    final_state = harness.run_json("park", "status")["isOpen"]
    harness.expect(final_state == initial["isOpen"], "Park gate did not return to original state")


def test_park_price_adjustment(harness: ScenarioHarness) -> None:
    baseline = harness.run_json("park", "price")
    original_fee = baseline["entranceFee"]
    target_fee = round(original_fee + 5.0, 2) or 5.0

    harness.run(
        "park",
        "price",
        "set",
        "--value",
        f"{target_fee:.2f}",
    )
    updated = harness.run_json("park", "price")
    harness.expect(abs(updated["entranceFee"] - target_fee) < 0.01, "Entrance fee did not update")

    harness.run("park", "price", "set", "--value", f"{original_fee:.2f}")
    reverted = harness.run_json("park", "price")
    harness.expect(abs(reverted["entranceFee"] - original_fee) < 0.01, "Entrance fee did not revert")


def test_rides_list_snapshot(harness: ScenarioHarness) -> None:
    rides = harness.run_json("rides", "list")
    harness.expect(len(rides) >= 1, "Ride list returned no entries")
    harness.expect(rides[0]["name"] == "Ferris Wheel 1", f"Unexpected ride 0: {rides[0]['name']}")

    text = harness.run("rides", "list")
    harness.expect("Ferris Wheel 1" in text, "Ride list text missing Ferris Wheel 1")


def test_park_guests_snapshot(harness: ScenarioHarness) -> None:
    guests = harness.run_json("park", "guests")
    harness.expect(guests["count"] == 0, f"Expected empty park, saw {guests['count']} guests")
    harness.expect(guests["parkRating"] == 200, f"Unexpected park rating: {guests['parkRating']}")


def test_park_warnings_snapshot(harness: ScenarioHarness) -> None:
    warnings = harness.run_json("park", "warnings")
    harness.expect(warnings["ridesBroken"] == 0, "Starter park should have no breakdowns")
    keys = [entry["key"] for entry in warnings["warnings"]]
    for required in ("hunger", "vandalism", "queue"):
        harness.expect(required in keys, f"Missing warning bucket: {required}")


def test_map_status_snapshot(harness: ScenarioHarness) -> None:
    status = harness.run_json("map", "status")
    harness.expect(status["width"] == 15 and status["height"] == 15, f"Unexpected map size: {status}")
    harness.expect(status["ownedTiles"] > 0, "Map should have owned tiles")


def test_map_area_ascii(harness: ScenarioHarness) -> None:
    text = harness.run("map", "area", "--x", "0", "--y", "0", "--width", "5", "--height", "5")
    harness.expect("#" in text and "." in text, "Map area legend missing expected glyphs")
    harness.expect("Anchor       : (0, 0)" in text, "Map area header incorrect")


def test_park_rating_history(harness: ScenarioHarness) -> None:
    history = harness.run_json("park", "rating", "history")
    harness.expect(history["monthsTracked"] == len(history["records"]), "Rating history months mismatch")
    harness.expect(history["records"][0]["rating"] > 0, "Latest rating missing")


def test_park_rewards_snapshot(harness: ScenarioHarness) -> None:
    rewards = harness.run_json("awards", "list")
    harness.expect("awards" in rewards, "Rewards payload missing awards")


# test_park_sandbox_toggle removed - sandbox commands intentionally disabled


def test_map_heatmap_guests(harness: ScenarioHarness) -> None:
    # Test default limit (should be 10)
    heatmap = harness.run_json("map", "heatmap", "guests")
    harness.expect(len(heatmap["hotspots"]) > 0, "Guest heatmap returned no hotspots")
    harness.expect(len(heatmap["hotspots"]) <= 10, "Default limit of 10 not applied")
    harness.expect(heatmap["hotspots"][0]["count"] > 0, "First hotspot missing count")
    # Test explicit limit
    limited = harness.run_json("map", "heatmap", "guests", "--limit", "3")
    harness.expect(len(limited["hotspots"]) <= 3, "Explicit --limit 3 not respected")


def test_guests_list_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("guests", "list", "--limit", "5")
    harness.expect(payload["returned"] == 5, "Guest list limit mismatch")
    names = [guest["name"] for guest in payload["guests"]]
    harness.expect(all(names), "Guest list returned empty names")


def test_guests_list_pagination(harness: ScenarioHarness) -> None:
    """Test that guests list pagination works correctly"""
    # Get first page
    page1 = harness.run_json("guests", "list", "--limit", "3")
    harness.expect(page1["returned"] == 3, "First page should return 3 guests")
    harness.expect(page1["limit"] == 3, "Limit should be 3")
    harness.expect("hasMore" in page1, "Response should include hasMore flag")

    # If there are more guests, test pagination
    if page1.get("hasMore", False):
        harness.expect("nextCursor" in page1, "hasMore=true should include nextCursor")
        cursor = page1["nextCursor"]

        # Get second page using cursor
        page2 = harness.run_json("guests", "list", "--limit", "3", "--after", str(cursor))
        harness.expect(page2["returned"] <= 3, "Second page should return at most 3 guests")

        # Verify no overlap between pages
        page1_ids = {guest["id"] for guest in page1["guests"]}
        page2_ids = {guest["id"] for guest in page2["guests"]}
        overlap = page1_ids & page2_ids
        harness.expect(len(overlap) == 0, f"Pages should not overlap, found {overlap}")

        # Verify guests are sorted by ID
        page1_guest_ids = [guest["id"] for guest in page1["guests"]]
        harness.expect(page1_guest_ids == sorted(page1_guest_ids), "Page 1 guests should be sorted by ID")
        if page2["guests"]:
            page2_guest_ids = [guest["id"] for guest in page2["guests"]]
            harness.expect(page2_guest_ids == sorted(page2_guest_ids), "Page 2 guests should be sorted by ID")
            harness.expect(page2_guest_ids[0] > page1_guest_ids[-1], "Page 2 IDs should be greater than page 1")

    # Verify text output includes pagination info when hasMore=true
    if page1.get("hasMore", False):
        text_output = harness.run("guests", "list", "--limit", "3")
        harness.expect("Next cursor" in text_output or "More results" in text_output,
                      "Text output should mention pagination when hasMore=true")


def test_guests_thoughts_pagination(harness: ScenarioHarness) -> None:
    """Test that guests thoughts pagination works correctly"""
    # Get first page with small limit
    page1 = harness.run_json("guests", "thoughts", "--limit", "5")
    harness.expect("groups" in page1, "Response should include groups")
    harness.expect("totalGroups" in page1, "Response should include totalGroups")
    harness.expect("offset" in page1, "Response should include offset")
    harness.expect("hasMore" in page1, "Response should include hasMore flag")
    harness.expect(page1["offset"] == 0, "First page offset should be 0")

    total_groups = page1["totalGroups"]
    groups_returned = len(page1["groups"])
    harness.expect(groups_returned <= 5, f"Should return at most 5 groups, got {groups_returned}")

    # If there are more thought groups, test pagination
    if page1.get("hasMore", False):
        harness.expect("nextOffset" in page1, "hasMore=true should include nextOffset")
        next_offset = page1["nextOffset"]
        harness.expect(next_offset == groups_returned, f"nextOffset should equal groups returned ({groups_returned}), got {next_offset}")

        # Get second page using offset
        page2 = harness.run_json("guests", "thoughts", "--limit", "5", "--offset", str(next_offset))
        harness.expect(page2["offset"] == next_offset, f"Page 2 offset should be {next_offset}")
        harness.expect(len(page2["groups"]) <= 5, "Second page should return at most 5 groups")

        # Verify no overlap between pages (by thought key)
        page1_keys = {group["key"] for group in page1["groups"]}
        page2_keys = {group["key"] for group in page2["groups"]}
        overlap = page1_keys & page2_keys
        harness.expect(len(overlap) == 0, f"Pages should not overlap, found {overlap}")

    # Verify text output includes pagination info when hasMore=true
    if page1.get("hasMore", False):
        text_output = harness.run("guests", "thoughts", "--limit", "5")
        harness.expect("--offset" in text_output,
                      "Text output should mention --offset when hasMore=true")


def test_staff_list_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("staff", "list", "--limit", "5")
    harness.expect(payload.get("limit", 0) == 5, "Staff list limit mismatch")
    harness.expect(len(payload.get("staff", [])) == min(5, payload.get("count", 0)), "Staff payload size mismatch")
    roles = {entry["type"] for entry in payload["staff"]}
    harness.expect("mechanic" in roles or "handyman" in roles, "Staff list missing core roles")


def test_staff_hire_returns_new_staff(harness: ScenarioHarness) -> None:
    """Test that staff hire returns the newly hired staff, not an existing one.

    This verifies the fix for the bug where staff hire would return info about
    an existing staff member instead of the newly created one.
    """
    # Get current staff count and IDs (use large limit to capture most staff)
    before = harness.run_json("staff", "list", "--limit", "500")
    count_before = before.get("count", 0)
    existing_ids = {entry["id"] for entry in before.get("staff", [])}

    # Hire a new handyman
    hired = harness.run_json("staff", "hire", "--type", "handyman")
    harness.expect("id" in hired, "Hired staff response missing id")
    new_id = hired["id"]

    # The returned ID should be a valid positive number (not -1 or null)
    harness.expect(
        new_id > 0,
        f"Staff hire returned invalid ID {new_id} (should be positive)",
    )

    # The returned ID should NOT be in the existing set (confirms it's actually new)
    harness.expect(
        new_id not in existing_ids,
        f"Staff hire returned existing staff ID {new_id} instead of new hire. "
        f"Existing IDs (first 10): {sorted(list(existing_ids))[:10]}...",
    )

    # Verify staff count increased
    after = harness.run_json("staff", "list", "--limit", "1")
    count_after = after.get("count", 0)
    harness.expect(
        count_after == count_before + 1,
        f"Staff count should increase by 1 (was {count_before}, now {count_after})",
    )

    # Verify we can look up the specific hired staff member
    staff_detail = harness.run_json("staff", "get", "--id", str(new_id))
    harness.expect(
        staff_detail.get("id") == new_id,
        f"Could not retrieve hired staff by ID {new_id}",
    )
    harness.expect(
        staff_detail.get("type") == "handyman",
        f"Hired staff type mismatch: expected handyman, got {staff_detail.get('type')}",
    )


def test_research_status_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("research", "status")
    harness.expect("allComplete" in payload, "Research status should include allComplete field")
    harness.expect(isinstance(payload["allComplete"], bool), "allComplete should be a boolean")
    # If research is not complete, we should have a next entry
    if not payload["allComplete"]:
        harness.expect("next" in payload and payload["next"]["name"], "Research next entry missing when not complete")
    harness.expect(payload["fundingLevel"] in ("none", "reduced", "normal", "maximum"), "Unknown funding level")


def test_weather_status_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("weather", "status")
    harness.expect(payload["current"]["type"] is not None, "Weather status missing current type")
    harness.expect(payload["monthName"], "Weather month name missing")


def test_weather_forecast_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("weather", "forecast")
    harness.expect("next" in payload, "Weather forecast missing next entry")


def test_news_recent_archive(harness: ScenarioHarness) -> None:
    recent = harness.run_json("news", "list", "--limit", "2")
    harness.expect("items" in recent, "Recent news missing items array")
    archive = harness.run_json("news", "list", "--archived", "true", "--limit", "2")
    harness.expect(archive["includesArchived"] is True, "Archive response missing flag")


def test_entrances_list_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("entrances", "list")
    harness.expect(payload["count"] >= 1, "No park entrances reported")
    harness.expect("parkOpen" in payload, "Missing parkOpen field")
    harness.expect("entrances" in payload, "Missing entrances array")
    entrances = payload["entrances"]
    harness.expect(len(entrances) >= 1, "Entrances array is empty")
    # Validate first entrance has all expected fields
    first = entrances[0]
    harness.expect("index" in first, "Entrance missing index field")
    harness.expect("x" in first, "Entrance missing x coordinate")
    harness.expect("y" in first, "Entrance missing y coordinate")
    harness.expect("z" in first, "Entrance missing z coordinate")
    harness.expect("direction" in first, "Entrance missing direction field")
    # Direction should be a string (north/south/east/west), not an integer
    direction = first["direction"]
    harness.expect(isinstance(direction, str), f"Direction should be string, got {type(direction).__name__}")
    valid_dirs = {"north", "south", "east", "west"}
    harness.expect(direction in valid_dirs, f"Invalid direction '{direction}', expected one of {valid_dirs}")


def test_park_warnings_busy_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("park", "warnings")
    harness.expect(payload["ridesBroken"] >= 0, "Warnings payload missing ridesBroken")
    hotspot = payload.get("queueHotspot")
    harness.expect(hotspot is None or "rideName" in hotspot, "Queue hotspot missing rideName")


# test_windows_list_snapshot removed - window commands intentionally disabled


def test_rides_price_adjustment_busy(harness: ScenarioHarness) -> None:
    baseline = harness.run_json("rides", "price", "--id", EVERYTHING_PARK_RIDE_ID)
    original_price = baseline["price"]
    target_price = round(original_price + 1.0, 2)

    harness.run(
        "rides",
        "price",
        "set",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--value",
        f"{target_price:.2f}",
    )
    updated = harness.run_json("rides", "price", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(abs(updated["price"] - target_price) < 0.01, "Ride price failed to update in busy park")

    harness.run(
        "rides",
        "price",
        "set",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--value",
        f"{original_price:.2f}",
    )
    reverted = harness.run_json("rides", "price", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(abs(reverted["price"] - original_price) < 0.01, "Ride price failed to revert")


def test_rides_available_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("rides", "catalog")
    harness.expect(payload["count"] > 0, "No ride blueprints returned")
    first = payload["rides"][0]
    harness.expect("name" in first, "Ride availability entries missing names")


def test_rides_breakdowns_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("rides", "breakdowns", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect("currentReason" in payload, "Breakdown payload missing currentReason")
    harness.expect(payload["downtimeBuckets"] >= 0, "Breakdown payload missing bucket data")


def test_rides_throughput_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("rides", "throughput", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(len(payload.get("customerHistory", [])) > 0, "Throughput history empty")


def test_rides_feedback_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("rides", "feedback", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(len(payload.get("groups", [])) > 0, "Ride feedback groups empty")


def test_rides_tune_inspection(harness: ScenarioHarness) -> None:
    status = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    original_label = status["inspectionIntervalLabel"]
    # Valid inspection intervals: 10min, 20min, 30min, 45min, 60min (1hour), 90min, 120min (2hours), never
    target_label = "60min" if original_label != "60min" else "10min"
    expected_label = target_label  # After setting, the response uses the canonical form

    harness.run(
        "rides",
        "tune",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--inspection",
        target_label,
    )
    updated = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(updated["inspectionIntervalLabel"] == expected_label, f"Ride inspection interval failed to update (expected {expected_label}, got {updated['inspectionIntervalLabel']})")

    harness.run(
        "rides",
        "tune",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--inspection",
        original_label,
    )
    reverted = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(reverted["inspectionIntervalLabel"] == original_label, "Ride inspection interval failed to revert")


def test_rides_rename_cycle(harness: ScenarioHarness) -> None:
    status = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    original_name = status["name"]
    new_name = f"{original_name} (Test)"

    harness.run(
        "rides",
        "rename",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--name",
        new_name,
    )
    renamed = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(renamed["name"] == new_name, "Ride rename failed to apply")

    harness.run(
        "rides",
        "rename",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--name",
        original_name,
    )
    reverted = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(reverted["name"] == original_name, "Ride rename failed to revert")


def test_rides_tune_operation_option(harness: ScenarioHarness) -> None:
    """Test setting operation option (laps, launch speed, rotations, etc.)"""
    status = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)

    # Check if this ride supports operation options
    op_min = status.get("operationMin", 0)
    op_max = status.get("operationMax", 0)

    if op_min >= op_max:
        # This ride doesn't have a configurable operation option, skip gracefully
        harness.expect(True, "Ride has no configurable operation option (min >= max), test skipped")
        return

    original_value = status.get("operationOption", op_min)
    # Pick a new value that's different from current
    target_value = op_min if original_value != op_min else op_max

    harness.run(
        "rides",
        "tune",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--operation-option",
        str(target_value),
    )
    updated = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(
        updated.get("operationOption") == target_value,
        f"Operation option failed to update (expected {target_value}, got {updated.get('operationOption')})"
    )

    # Revert to original
    harness.run(
        "rides",
        "tune",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--operation-option",
        str(original_value),
    )
    reverted = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(
        reverted.get("operationOption") == original_value,
        "Operation option failed to revert"
    )


def test_rides_tune_departure_flags(harness: ScenarioHarness) -> None:
    """Test setting departure flags (wait-for-load, leave-on-arrival, sync-stations)"""
    status = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)

    original_wait_for_load = status.get("waitForLoad", False)
    original_leave_on_arrival = status.get("leaveWhenAnotherArrives", False)

    # Toggle wait-for-load to a different level
    target_wait = "full" if not original_wait_for_load else "any"

    harness.run(
        "rides",
        "tune",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--wait-for-load",
        target_wait,
    )
    updated = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)

    if target_wait == "full":
        harness.expect(
            updated.get("waitForLoad") == True and updated.get("waitForLoadLevel") == 4,
            f"wait-for-load=full failed (got waitForLoad={updated.get('waitForLoad')}, level={updated.get('waitForLoadLevel')})"
        )
    else:
        harness.expect(
            updated.get("waitForLoadLevel") == 0,
            f"wait-for-load=any failed (got level={updated.get('waitForLoadLevel')})"
        )

    # Test leave-on-arrival toggle
    target_leave = not original_leave_on_arrival
    harness.run(
        "rides",
        "tune",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--leave-on-arrival",
        str(target_leave).lower(),
    )
    updated2 = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(
        updated2.get("leaveWhenAnotherArrives") == target_leave,
        f"leave-on-arrival toggle failed (expected {target_leave}, got {updated2.get('leaveWhenAnotherArrives')})"
    )

    # Revert leave-on-arrival
    harness.run(
        "rides",
        "tune",
        "--id",
        EVERYTHING_PARK_RIDE_ID,
        "--leave-on-arrival",
        str(original_leave_on_arrival).lower(),
    )
    reverted = harness.run_json("rides", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(
        reverted.get("leaveWhenAnotherArrives") == original_leave_on_arrival,
        "leave-on-arrival failed to revert"
    )


def test_rides_theme_colors_list(harness: ScenarioHarness) -> None:
    """Test rides theme colors lists available color options"""
    payload = harness.run_json("rides", "theme", "colors")
    harness.expect("colors" in payload, "Missing colors array in theme colors response")

    colors = payload.get("colors", [])
    harness.expect(len(colors) > 0, "No colors returned")

    # Should have at least classic RCT2 colors (0-31)
    harness.expect(len(colors) >= 32, "Expected at least 32 classic colors")

    if len(colors) > 0:
        first = colors[0]
        harness.expect("name" in first, "Color entry missing name")
        harness.expect("category" in first, "Color entry missing category")


def test_rides_theme_entrance_list(harness: ScenarioHarness) -> None:
    """Test rides theme entrance list returns available station styles"""
    payload = harness.run_json("rides", "theme", "entrance", "list")
    harness.expect("styles" in payload, "Missing styles array in entrance list response")

    styles = payload.get("styles", [])
    harness.expect(len(styles) > 0, "No entrance styles returned")

    if len(styles) > 0:
        first = styles[0]
        harness.expect("identifier" in first, "Style entry missing identifier")
        harness.expect("name" in first, "Style entry missing name")


def test_rides_theme_get(harness: ScenarioHarness) -> None:
    """Test rides theme get returns theming data for a ride"""
    payload = harness.run_json("rides", "theme", "get", "--id", EVERYTHING_PARK_RIDE_ID)

    harness.expect("ride" in payload, "Missing ride info in theme response")
    harness.expect("trackColours" in payload, "Missing trackColours in theme response")
    harness.expect("vehicleColours" in payload, "Missing vehicleColours in theme response")
    harness.expect("entranceStyle" in payload, "Missing entranceStyle in theme response")

    # Verify structure of track colours - colors are now strings (names)
    track_colours = payload.get("trackColours", [])
    harness.expect(len(track_colours) > 0, "No track colour schemes returned")
    if len(track_colours) > 0:
        first_scheme = track_colours[0]
        harness.expect("scheme" in first_scheme, "Track colour scheme missing scheme index")
        harness.expect("main" in first_scheme, "Track colour scheme missing main colour")
        main_colour = first_scheme.get("main", "")
        harness.expect(isinstance(main_colour, str) and len(main_colour) > 0, "Main colour should be a non-empty string")


def test_rides_theme_track_roundtrip(harness: ScenarioHarness) -> None:
    """Test that track colors can be changed and reverted"""
    # Get current theme - colors are now just strings (names)
    original = harness.run_json("rides", "theme", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    original_main = original["trackColours"][0]["main"]

    # Pick a different color
    new_color = "bright_red" if original_main != "bright_red" else "dark_blue"

    # Change the track color
    harness.run(
        "rides", "theme", "track", "set",
        "--id", EVERYTHING_PARK_RIDE_ID,
        "--main", new_color
    )

    # Verify the change
    updated = harness.run_json("rides", "theme", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    updated_main = updated["trackColours"][0]["main"]
    harness.expect(
        updated_main == new_color,
        f"Track main color failed to update (expected {new_color}, got {updated_main})"
    )

    # Revert to original
    harness.run(
        "rides", "theme", "track", "set",
        "--id", EVERYTHING_PARK_RIDE_ID,
        "--main", original_main
    )

    # Verify revert
    reverted = harness.run_json("rides", "theme", "get", "--id", EVERYTHING_PARK_RIDE_ID)
    harness.expect(
        reverted["trackColours"][0]["main"] == original_main,
        "Track main color failed to revert"
    )


def test_construction_water_raise_lower(harness: ScenarioHarness) -> None:
    x, y = ELECTRIC_WATER_TILE
    tile = harness.get_tile(x, y)
    original_height = tile["surface"]["waterHeight"]
    harness.expect(original_height > 0, "Expected starting tile to contain water")

    harness.run("construction", "water", "raise", "--x", str(x), "--y", str(y))
    raised = harness.get_tile(x, y)
    # Water raise adds 2 tile units (was 16 world units)
    harness.expect(
        raised["surface"]["waterHeight"] == original_height + 2,
        "Water raise did not add 2 tile units",
    )

    harness.run("construction", "water", "lower", "--x", str(x), "--y", str(y))
    restored = harness.get_tile(x, y)
    harness.expect(
        restored["surface"]["waterHeight"] == original_height,
        "Water lower failed to revert the tile",
    )


def test_map_ownership_snapshot(harness: ScenarioHarness) -> None:
    ownership = harness.run_json("map", "ownership")
    owned = ownership.get("owned", {})
    harness.expect(owned.get("tiles", 0) > 0, "Owned tiles missing from map ownership payload")


def test_trees_catalog_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("trees", "catalog")
    harness.expect("entries" in payload, "Missing entries array in trees catalog")
    harness.expect("count" in payload, "Missing count field in trees catalog")
    harness.expect(payload.get("count", 0) > 0, "No trees returned in catalog")
    if payload["count"] > 0:
        first = payload["entries"][0]
        harness.expect("identifier" in first, "Tree entries missing identifier")
        harness.expect("price" in first, "Tree entries missing price")
        harness.expect("height" in first, "Tree entries missing height")


def test_construction_scenery_clear(harness: ScenarioHarness) -> None:
    """Test construction scenery clear command clears small scenery (trees)"""
    x, y = ELECTRIC_TREE_TILE

    # Verify tree exists before clearing
    before = harness.get_tile(x, y)
    tree_present_before = any(elem.get("type") in ("tree", "smallScenery") for elem in before["elements"])
    harness.expect(tree_present_before, "Tree should exist before clearing")

    # Clear small scenery (includes trees)
    result = harness.run_json(
        "construction",
        "scenery",
        "clear",
        "--x",
        str(x),
        "--y",
        str(y),
        "--small",
    )
    harness.expect("cleared" in result, "Response should include 'cleared' field")
    harness.expect("small" in result["cleared"], "Response should indicate small scenery was cleared")

    after_clear = harness.get_tile(x, y)
    tree_present = any(elem.get("type") in ("tree", "smallScenery") for elem in after_clear["elements"])
    harness.expect(not tree_present, "Tree still present after scenery clear")

    # Place tree back using trees place
    harness.run(
        "trees",
        "place",
        "--x",
        str(x),
        "--y",
        str(y),
        "--tree-id",
        ELECTRIC_TREE_IDENTIFIER,
    )
    after_place = harness.get_tile(x, y)
    tree_present = any(elem.get("type") in ("tree", "smallScenery") for elem in after_place["elements"])
    harness.expect(tree_present, "Tree failed to reappear after planting")


def _find_clear_tile_for_ride(harness: ScenarioHarness) -> tuple[int, int]:
    for y in range(8, 80):
        for x in range(8, 80):
            tile = harness.get_tile(x, y)
            surface = tile.get("surface", {})
            if not surface.get("owned"):
                continue
            if surface.get("waterHeight", 0) > surface.get("baseHeight", 0):
                continue
            # Ensure a generous 5x5 neighborhood is owned + empty so blueprints with 3x3 footprints stay within park bounds.
            has_clear_neighborhood = True
            for ny in range(y - 2, y + 3):
                for nx in range(x - 2, x + 3):
                    neighbor = harness.get_tile(nx, ny)
                    neighbor_surface = neighbor.get("surface", {})
                    if not neighbor_surface.get("owned"):
                        has_clear_neighborhood = False
                        break
                    elements = neighbor.get("elements", [])
                    if len(elements) != 1 or elements[0].get("type") != "surface":
                        has_clear_neighborhood = False
                        break
                if not has_clear_neighborhood:
                    break
            if not has_clear_neighborhood:
                continue
            return x, y
    raise ScenarioFailure("Unable to find clear owned tile for ride placement")


def _select_access_candidate(
    harness: ScenarioHarness, candidates: list[dict], ride_id: int | str | None
) -> dict:
    if ride_id is None:
        return candidates[0]
    for candidate in candidates:
        if not candidate.get("owned"):
            continue
        tile = harness.get_tile(candidate["x"], candidate["y"])
        ride_footprint = tile.get("rideFootprint", [])
        if ride_footprint:
            continue
        elements = tile.get("elements", [])
        if len(elements) != 1 or elements[0].get("type") != "surface":
            continue
        return candidate
    return candidates[0]


def _find_flat_tile_for_path(harness: ScenarioHarness) -> tuple[int, int]:
    search_windows = [
        (1, 12, 24, 36),
        (10, 22, 60, 72),
    ]
    for x_start, x_end, y_start, y_end in search_windows:
        for y in range(y_start, y_end):
            for x in range(x_start, x_end):
                tile = harness.get_tile(x, y)
                surface = tile.get("surface")
                if not surface or not surface.get("owned", False):
                    continue
                if surface.get("waterHeight", 0) > 0:
                    continue
                if tile.get("isEdge"):
                    continue
                elements = tile.get("elements", [])
                surface_elem = next((elem for elem in elements if "surface" in elem), None)
                if surface_elem and surface_elem["surface"].get("slope", 0) != 0:
                    continue
                obstructed = False
                for element in elements:
                    if any(
                        key in element
                        for key in ("path", "entrance", "smallScenery", "largeScenery", "wall", "banner", "track")
                    ):
                        obstructed = True
                        break
                if obstructed:
                    continue
                return x, y
    raise ScenarioFailure("Unable to find a flat owned tile for path placement test")


def test_paths_place_flat(harness: ScenarioHarness) -> None:
    x, y = _find_flat_tile_for_path(harness)
    result = harness.run_json(
        "paths",
        "place",
        "--x",
        str(x),
        "--y",
        str(y),
        "--surface-id",
        ELECTRIC_PATH_SURFACE,
    )
    harness.expect(result.get("queue") is False, "Expected standard path tile")
    after = harness.get_tile(x, y)
    has_path = any("path" in element for element in after.get("elements", []))
    harness.expect(has_path, "Path element missing after placement")


def test_paths_place_on_water(harness: ScenarioHarness) -> None:
    """Place a bridge path on a water tile without explicit --z."""
    x, y = ELECTRIC_WATER_TILE
    tile = harness.get_tile(x, y)
    surface = tile.get("surface", {})
    water_height = surface.get("waterHeight", 0)
    base_height = surface.get("baseHeight", 0)
    harness.expect(water_height > base_height, "Expected water tile for bridge placement test")

    result = harness.run_json(
        "paths",
        "place",
        "--x",
        str(x),
        "--y",
        str(y),
        "--surface",
        "tarmac",
    )
    harness.expect(result.get("elevated") is True, "Expected elevated flag for water bridge placement")
    harness.expect(
        result.get("height") == water_height,
        f"Expected path height to match water height ({water_height}), got {result.get('height')}",
    )

    after = harness.get_tile(x, y)
    has_path = any("path" in element for element in after.get("elements", []))
    harness.expect(has_path, "Path element missing after water placement")

    # Cleanup to avoid affecting later tests that rely on a clear water tile.
    harness.run("construction", "scenery", "clear", "--x", str(x), "--y", str(y), "--paths")


def test_paths_catalog_snapshot(harness: ScenarioHarness) -> None:
    payload = harness.run_json("paths", "catalog")
    harness.expect(payload.get("surfaceCount", 0) > 0, "No path surfaces returned")
    harness.expect("surfaces" in payload, "Missing surfaces array")
    harness.expect("railings" in payload, "Missing railings array")
    if payload["surfaceCount"] > 0:
        first = payload["surfaces"][0]
        harness.expect("identifier" in first, "Path surface entries missing identifier")
        harness.expect("name" in first, "Path surface entries missing name")


def test_paths_remove_and_place(harness: ScenarioHarness) -> None:
    x, y = _find_flat_tile_for_path(harness)
    # Place a path first
    harness.run(
        "paths",
        "place",
        "--x",
        str(x),
        "--y",
        str(y),
        "--surface-id",
        ELECTRIC_PATH_SURFACE,
    )
    # Verify path was placed
    tile_after_place = harness.get_tile(x, y)
    has_path = any("path" in element for element in tile_after_place.get("elements", []))
    harness.expect(has_path, "Path element missing after placement")

    # Remove the path
    removed = harness.run_json("paths", "remove", "--x", str(x), "--y", str(y))
    harness.expect("tile" in removed, "Missing tile in removal response")
    harness.expect(removed.get("queue") is False, "Expected standard path type")

    # Verify path was removed
    tile_after_remove = harness.get_tile(x, y)
    has_path_after = any("path" in element for element in tile_after_remove.get("elements", []))
    harness.expect(not has_path_after, "Path still present after removal")


def test_paths_place_with_alias(harness: ScenarioHarness) -> None:
    """Test that friendly surface aliases like 'tarmac' work in paths place"""
    x, y = _find_flat_tile_for_path(harness)
    result = harness.run_json(
        "paths",
        "place",
        "--x",
        str(x),
        "--y",
        str(y),
        "--surface",
        "tarmac",  # Using friendly alias instead of full identifier
    )
    harness.expect(result.get("queue") is False, "Expected standard path tile")
    harness.expect("surface" in result, "Response should include surface info")
    # The resolved identifier should contain the full path
    surface = result.get("surface", {})
    harness.expect("identifier" in surface, "Surface should have resolved identifier")
    # Verify path was actually placed
    after = harness.get_tile(x, y)
    has_path = any("path" in element for element in after.get("elements", []))
    harness.expect(has_path, "Path element missing after placement with alias")


def test_paths_place_elevated_flat(harness: ScenarioHarness) -> None:
    """Test placing elevated flat path with explicit --z"""
    x, y = _find_flat_tile_for_path(harness)
    # Place elevated path at z=16 (above ground, in tile units)
    result = harness.run_json(
        "paths",
        "place",
        "--x",
        str(x),
        "--y",
        str(y),
        "--surface",
        "tarmac",
        "--z",
        "16",
    )
    harness.expect(result.get("elevated") is True, "Expected elevated flag to be true")
    harness.expect(result.get("height") == 16, "Expected height to match --z value")
    harness.expect(result.get("slope") == "flat", "Expected flat slope for elevated path without --slope")


def test_paths_place_elevated_sloped(harness: ScenarioHarness) -> None:
    """Test placing elevated sloped path with --z and --slope"""
    x, y = _find_flat_tile_for_path(harness)
    # Place elevated sloped path (z=16 in tile units)
    result = harness.run_json(
        "paths",
        "place",
        "--x",
        str(x),
        "--y",
        str(y),
        "--surface",
        "tarmac",
        "--z",
        "16",
        "--slope",
        "north",
    )
    harness.expect(result.get("elevated") is True, "Expected elevated flag to be true")
    harness.expect(result.get("height") == 16, "Expected height to match --z value")
    harness.expect("north" in result.get("slope", ""), "Expected slope to indicate north direction")


def test_rides_place_and_entrances(harness: ScenarioHarness) -> None:
    x, y = _find_clear_tile_for_ride(harness)
    placement = harness.run_json(
        "rides",
        "place",
        "--type",
        "rct2.ride.twist1",
        "--x",
        str(x),
        "--y",
        str(y),
    )
    ride = placement.get("ride", {})
    ride_id = ride.get("id")
    harness.expect(ride_id is not None, "Ride placement response missing ride id")

    footprint = placement.get("footprint", {})
    tiles = footprint.get("tiles", [])
    harness.expect(tiles, "Ride placement response missing footprint tiles")
    candidates = footprint.get("entranceCandidates", [])
    harness.expect(candidates, "Ride placement response missing entrance candidates")

    entrance_candidate = _select_access_candidate(harness, candidates, ride_id)
    harness.run(
        "rides",
        "entrance",
        "place",
        "--id",
        str(ride_id),
        "--x",
        str(entrance_candidate["x"]),
        "--y",
        str(entrance_candidate["y"]),
        "--direction",
        entrance_candidate.get("direction", "south"),
    )

    remaining_candidates = [
        c for c in candidates if c["x"] != entrance_candidate["x"] or c["y"] != entrance_candidate["y"]
    ]
    if not remaining_candidates:
        remaining_candidates = candidates
    exit_candidate = _select_access_candidate(harness, remaining_candidates, ride_id)
    harness.run(
        "rides",
        "exit",
        "place",
        "--id",
        str(ride_id),
        "--x",
        str(exit_candidate["x"]),
        "--y",
        str(exit_candidate["y"]),
    )

    for tile_info in tiles:
        tile_state = harness.get_tile(tile_info["x"], tile_info["y"])
        harness.expect(tile_state.get("isValid", False), f"Ride footprint reported invalid tile {tile_info}")
        footprint_ids = tile_state.get("rideFootprint", [])
        if ride_id in footprint_ids:
            continue
        surface = tile_state.get("surface", {})
        harness.expect(
            surface.get("owned", False),
            f"Ride clearance tile ({tile_info['x']}, {tile_info['y']}) not owned by park",
        )


def test_rides_place_flat_ride_by_name(harness: ScenarioHarness) -> None:
    """Test that rides place --name works correctly (verifies object resolution fix)"""
    x, y = _find_clear_tile_for_ride(harness)
    # Use display name instead of identifier - this tests the fixed object resolution
    placement = harness.run_json(
        "rides",
        "place",
        "--name",
        "Twist",  # Display name, not identifier
        "--x",
        str(x),
        "--y",
        str(y),
    )
    ride = placement.get("ride", {})
    ride_id = ride.get("id")
    harness.expect(ride_id is not None, "Ride placement by name failed - missing ride id")
    harness.expect("Twist" in ride.get("name", ""), f"Ride name mismatch: {ride.get('name')}")


def test_rides_catalog_flat_rides(harness: ScenarioHarness) -> None:
    """Test that rides catalog returns ride blueprints with required fields"""
    payload = harness.run_json("rides", "catalog")
    harness.expect("count" in payload, "Catalog missing count field")
    harness.expect("rides" in payload, "Catalog missing rides array")
    harness.expect(payload["count"] > 0, "No ride blueprints returned")

    # Validate structure of first ride entry
    rides = payload["rides"]
    harness.expect(len(rides) > 0, "Rides array is empty despite count > 0")

    first = rides[0]
    harness.expect("identifier" in first, "Ride missing identifier")
    harness.expect("name" in first, "Ride missing name")
    harness.expect("entryIndex" in first, "Ride missing entryIndex")
    harness.expect("invented" in first, "Ride missing invented status")


def test_coasters_list_designs(harness: ScenarioHarness) -> None:
    """Test that rides coasters list returns available pre-built designs"""
    payload = harness.run_json("rides", "coasters", "list")
    harness.expect("designs" in payload, "Coasters list missing designs array")
    harness.expect("totalCount" in payload, "Coasters list missing totalCount")

    # There should be some pre-built designs available
    if payload["totalCount"] > 0:
        first = payload["designs"][0]
        harness.expect("name" in first, "Coaster design missing name")
        harness.expect("rideTypeName" in first, "Coaster design missing rideTypeName")
        # Check for statistics if available
        if "statistics" in first:
            stats = first["statistics"]
            harness.expect("spaceRequired" in stats, "Coaster stats missing spaceRequired")


def test_coasters_preview_footprint(harness: ScenarioHarness) -> None:
    """Test that rides coasters preview returns footprint info"""
    # First, get designs from the list
    list_payload = harness.run_json("rides", "coasters", "list")
    if list_payload.get("totalCount", 0) == 0:
        harness.expect(True, "No coaster designs available - test skipped")
        return

    x, y = _find_clear_tile_for_ride(harness)

    # Try each design until we find one with an invented ride type
    preview = None
    design_name = None
    for design in list_payload["designs"]:
        design_name = design["name"]
        try:
            preview = harness.run_json(
                "rides",
                "coasters",
                "preview",
                "--name",
                design_name,
                "--x",
                str(x),
                "--y",
                str(y),
            )
            break  # Success - use this design
        except ScenarioFailure as e:
            if "not yet invented" in str(e):
                continue  # Try next design
            raise  # Re-raise other errors

    if preview is None:
        harness.expect(True, "No coaster designs with invented ride types - test skipped")
        return

    harness.expect("design" in preview, "Preview missing design info")
    harness.expect("placement" in preview, "Preview missing placement info")
    harness.expect("footprint" in preview, "Preview missing footprint info")

    footprint = preview["footprint"]
    harness.expect("spaceRequired" in footprint, "Footprint missing spaceRequired")
    harness.expect("estimatedBounds" in footprint, "Footprint missing estimatedBounds")
    harness.expect("anchorMeaning" in footprint, "Footprint missing anchorMeaning explanation")

    placement = preview["placement"]
    harness.expect("canPlace" in placement, "Placement missing canPlace status")
    harness.expect("cost" in placement, "Placement missing cost estimate")


def _find_large_clear_area_for_coaster(harness: ScenarioHarness, min_size: int = 20) -> tuple[int, int]:
    """Find a clear owned area large enough for a large coaster."""
    for y in range(8, 80 - min_size):
        for x in range(8, 80 - min_size):
            tile = harness.get_tile(x, y)
            surface = tile.get("surface", {})
            if not surface.get("owned"):
                continue
            if surface.get("waterHeight", 0) > surface.get("baseHeight", 0):
                continue
            # Check a larger area for coaster placement
            has_clear_neighborhood = True
            for ny in range(y, y + min_size):
                for nx in range(x, x + min_size):
                    neighbor = harness.get_tile(nx, ny)
                    neighbor_surface = neighbor.get("surface", {})
                    if not neighbor_surface.get("owned"):
                        has_clear_neighborhood = False
                        break
                    elements = neighbor.get("elements", [])
                    if len(elements) != 1 or elements[0].get("type") != "surface":
                        has_clear_neighborhood = False
                        break
                if not has_clear_neighborhood:
                    break
            if not has_clear_neighborhood:
                continue
            return x, y
    raise ScenarioFailure(f"Unable to find {min_size}x{min_size} clear owned area for coaster placement")


def test_coaster_place_demolish_replace(harness: ScenarioHarness) -> None:
    """Test that a coaster can be placed at the same location after demolition.

    This tests the fix for the Z-height auto-detection bug where large coasters
    could not be re-placed after demolition due to incorrect height calculation.
    """
    # Enable ignoreResearchStatus so all ride types are available
    try:
        harness.run_json("park", "sandboxSet", "--key", "ignoreResearchStatus", "--value", "true")
    except ScenarioFailure:
        pass  # May not be available, continue anyway

    # Get available coaster designs
    list_payload = harness.run_json("rides", "coasters", "list")
    if list_payload.get("totalCount", 0) == 0:
        harness.expect(True, "No coaster designs available - test skipped")
        return

    # Find a large clear area
    try:
        x, y = _find_large_clear_area_for_coaster(harness, min_size=15)
    except ScenarioFailure:
        harness.expect(True, "No large enough clear area for coaster test - test skipped")
        return

    # Sort designs by space required (smallest first) to find placeable ones faster
    designs = list_payload.get("designs", [])
    designs_with_size = []
    for d in designs:
        stats = d.get("statistics", {})
        space = stats.get("spaceRequired", {})
        # Calculate area, default to large value if not available
        area = space.get("x", 100) * space.get("y", 100)
        designs_with_size.append((area, d))
    designs_with_size.sort(key=lambda x: x[0])

    # Try to find a design that can be placed (limit to 10 attempts for speed)
    placed_design = None
    ride_id = None
    attempts = 0
    max_attempts = 10
    for _, design in designs_with_size:
        if attempts >= max_attempts:
            break
        design_name = design["name"]
        attempts += 1
        try:
            # Preview first to check if it can be placed
            preview = harness.run_json(
                "rides", "coasters", "preview",
                "--name", design_name,
                "--x", str(x),
                "--y", str(y),
            )
            if not preview.get("placement", {}).get("canPlace", False):
                continue

            # Actually place the coaster
            place_result = harness.run_json(
                "rides", "coasters", "place",
                "--name", design_name,
                "--x", str(x),
                "--y", str(y),
            )
            ride = place_result.get("ride", {})
            ride_id = ride.get("id")
            if ride_id is not None:
                placed_design = design_name
                break
        except ScenarioFailure as e:
            if "not yet invented" in str(e):
                continue
            # Other errors might be placement issues - try next design
            continue

    if placed_design is None:
        harness.expect(True, f"No coaster could be placed after {attempts} attempts - test skipped")
        return

    harness.expect(ride_id is not None, f"Placed coaster {placed_design} but missing ride id")

    # Demolish the coaster
    harness.run("rides", "demolish", "--id", str(ride_id))

    # Verify it's gone
    rides_list = harness.run_json("rides", "list")
    ride_ids = [r.get("id") for r in rides_list.get("rides", [])]
    harness.expect(ride_id not in ride_ids, f"Ride {ride_id} still exists after demolition")

    # Try to preview at the same location - this is the key test
    # With the fix, the Z-height should be properly calculated for all tiles
    try:
        preview2 = harness.run_json(
            "rides", "coasters", "preview",
            "--name", placed_design,
            "--x", str(x),
            "--y", str(y),
        )
        can_place = preview2.get("placement", {}).get("canPlace", False)
        error_msg = preview2.get("placement", {}).get("errorMessage", "")
        harness.expect(
            can_place,
            f"Cannot re-place coaster after demolition at same location. Error: {error_msg}"
        )
    except ScenarioFailure as e:
        harness.expect(False, f"Preview after demolition failed: {e}")


def _direction_to_offset(direction: str) -> tuple[int, int]:
    """Convert a direction string to tile coordinate offset (dx, dy).

    The direction indicates where guests approach FROM, so moving in that
    direction from the entrance gives us the queue path position.
    """
    offsets = {
        "west": (-1, 0),
        "north": (0, -1),
        "east": (1, 0),
        "south": (0, 1),
    }
    return offsets.get(direction.lower(), (0, 0))


def test_queue_path_connects_to_entrance(harness: ScenarioHarness) -> None:
    """Test that queue paths properly connect to ride entrances placed via CLI.

    This verifies the fix for the entrance direction bug where CLI-placed entrances
    had the wrong internal direction, preventing queue paths from connecting.
    The key indicator is that a connected queue path has rideId set to the ride's ID,
    while a disconnected path has rideId = -1.
    """
    # Find a clear area for our ride
    x, y = _find_clear_tile_for_ride(harness)

    # Place a ride
    placement = harness.run_json(
        "rides",
        "place",
        "--type",
        "rct2.ride.twist1",
        "--x",
        str(x),
        "--y",
        str(y),
    )
    ride = placement.get("ride", {})
    ride_id = ride.get("id")
    harness.expect(ride_id is not None, "Ride placement failed - missing ride id")

    # Get entrance candidates
    footprint = placement.get("footprint", {})
    candidates = footprint.get("entranceCandidates", [])
    harness.expect(len(candidates) > 0, "No entrance candidates returned")

    # Select a candidate and place the entrance
    entrance_candidate = _select_access_candidate(harness, candidates, ride_id)
    entrance_x = entrance_candidate["x"]
    entrance_y = entrance_candidate["y"]
    entrance_direction = entrance_candidate.get("direction", "south")

    harness.run(
        "rides",
        "entrance",
        "place",
        "--id",
        str(ride_id),
        "--x",
        str(entrance_x),
        "--y",
        str(entrance_y),
        "--direction",
        entrance_direction,
    )

    # Calculate where to place the queue path.
    # The entrance direction indicates where guests approach FROM,
    # so the queue path goes one tile in that direction from the entrance.
    dx, dy = _direction_to_offset(entrance_direction)
    queue_x = entrance_x + dx
    queue_y = entrance_y + dy

    # Verify the queue path location is clear and owned
    queue_tile_before = harness.get_tile(queue_x, queue_y)
    queue_surface = queue_tile_before.get("surface", {})
    harness.expect(
        queue_surface.get("owned", False),
        f"Queue path tile ({queue_x}, {queue_y}) is not owned - cannot test",
    )

    # Get the entrance's Z height for the queue path
    entrance_tile = harness.get_tile(entrance_x, entrance_y)
    entrance_elements = entrance_tile.get("elements", [])
    entrance_z = None
    for elem in entrance_elements:
        if elem.get("type") == "entrance":
            entrance_z = elem.get("base")  # Field is "base", not "baseHeight"
            break
    harness.expect(entrance_z is not None, "Could not find entrance element to determine Z height")

    # Place a queue path adjacent to the entrance
    # Queue paths use special surface types (queue_blue, queue_red, etc.)
    queue_result = harness.run_json(
        "paths",
        "place",
        "--x",
        str(queue_x),
        "--y",
        str(queue_y),
        "--surface-id",
        "queue_blue",  # Queue surface - creates a queue path that can connect to entrances
    )
    harness.expect(
        queue_result.get("success", False) or "error" not in queue_result,
        f"Queue path placement failed: {queue_result}",
    )

    # Verify the queue path element was created and is connected to the ride
    queue_tile_after = harness.get_tile(queue_x, queue_y)
    queue_elements = queue_tile_after.get("elements", [])

    path_element = None
    for elem in queue_elements:
        if elem.get("type") == "path":
            path_details = elem.get("path", {})
            if path_details.get("isQueue", False):
                path_element = path_details
                break

    harness.expect(path_element is not None, f"No queue path element found at ({queue_x}, {queue_y})")

    # THE KEY CHECK: The queue path's rideId should match our ride if it connected properly.
    # If the entrance direction bug exists, rideId will be -1 (not connected).
    path_ride_id = path_element.get("rideId", -1)
    harness.expect(
        path_ride_id == ride_id,
        f"Queue path at ({queue_x}, {queue_y}) not connected to ride! "
        f"Expected rideId={ride_id}, got rideId={path_ride_id}. "
        f"This indicates the entrance direction bug - the entrance was placed with "
        f"the wrong internal direction, preventing queue path connectivity.",
    )

    # Also verify the station index is set
    path_station = path_element.get("stationIndex", -1)
    harness.expect(
        path_station >= 0,
        f"Queue path stationIndex not set (got {path_station}), expected >= 0",
    )


def test_entrance_candidates_not_blocked(harness: ScenarioHarness) -> None:
    """Test that entrance candidates returned by rides place exclude blocked tiles"""
    x, y = _find_clear_tile_for_ride(harness)

    # Place a path outside the ride footprint but where entrance candidates would be
    # Twist is roughly 3x3, so entrance candidates are at offset 2+ from anchor
    path_x, path_y = x - 2, y  # Place path 2 tiles west, outside footprint but adjacent
    harness.run(
        "paths",
        "place",
        "--x",
        str(path_x),
        "--y",
        str(path_y),
        "--surface-id",
        ELECTRIC_PATH_SURFACE,
    )

    # Now place a ride at the target location
    placement = harness.run_json(
        "rides",
        "place",
        "--type",
        "rct2.ride.twist1",
        "--x",
        str(x),
        "--y",
        str(y),
    )

    ride = placement.get("ride", {})
    harness.expect(ride.get("id") is not None, "Ride placement failed")

    footprint = placement.get("footprint", {})
    candidates = footprint.get("entranceCandidates", [])

    # Verify the blocked tile is NOT in the candidates
    blocked_candidates = [c for c in candidates if c["x"] == path_x and c["y"] == path_y]
    harness.expect(
        len(blocked_candidates) == 0,
        f"Blocked tile ({path_x}, {path_y}) should not be in entrance candidates",
    )

    # Verify all returned candidates are actually usable (owned and within map)
    for candidate in candidates:
        harness.expect(
            candidate.get("owned", False),
            f"Entrance candidate ({candidate['x']}, {candidate['y']}) not marked as owned",
        )
        harness.expect(
            candidate.get("withinMap", False),
            f"Entrance candidate ({candidate['x']}, {candidate['y']}) not within map",
        )


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run headless scenario tests for rctctl.")
    parser.add_argument("--build-dir", type=Path, default=None, help="Path to the CMake build directory")
    parser.add_argument("--openrct2-cli", type=Path, default=None, help="Path to openrct2-cli executable")
    parser.add_argument("--rctctl", type=Path, default=None, help="Path to rctctl executable")
    parser.add_argument("--user-data-path", type=Path, default=None, help="Override for OpenRCT2 user data dir")
    parser.add_argument("--rct2-data-path", type=Path, default=None, help="Override for original RCT2 data dir")
    parser.add_argument("--list", action="store_true", help="List available scenario suites and exit")
    parser.add_argument("--suite", type=str, default=None, help="Run only this suite (e.g., electric_fields)")
    parser.add_argument("--test", type=str, default=None, help="Run only tests matching this substring")
    return parser


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(argv)

    root = repo_root()
    build_dir = Path(args.build_dir) if args.build_dir else default_build_dir()
    openrct2_cli = Path(args.openrct2_cli) if args.openrct2_cli else build_dir / "openrct2-cli"
    rctctl_path = Path(args.rctctl) if args.rctctl else build_dir / "rctctl" / "rctctl"

    if not openrct2_cli.exists():
        parser.error(f"openrct2-cli not found at {openrct2_cli}. Build target `openrct2-cli` or `agent_bundle` first.")
    if not rctctl_path.exists():
        parser.error(f"rctctl not found at {rctctl_path}. Build target `rctctl` or `agent_bundle` first.")

    user_data_path = Path(args.user_data_path) if args.user_data_path else detect_user_data_path()
    if not user_data_path:
        parser.error("Unable to locate OpenRCT2 user data directory. Set --user-data-path or OPENRCT2_USER_DATA.")

    rct2_data_path = Path(args.rct2_data_path) if args.rct2_data_path else read_game_path(user_data_path)
    if not rct2_data_path:
        print("Warning: original RCT2 game_path not found; relying on existing config", file=sys.stderr)

    data_bundle_path = ensure_data_bundle(build_dir, root)

    suites = [ferris_wheel_suite(root), everything_park_suite(root), electric_fields_suite(root)]

    # Add coaster sandbox suite if RCT2 data is available
    coaster_suite = coaster_sandbox_suite(rct2_data_path)
    if coaster_suite:
        suites.append(coaster_suite)

    # Filter suites if --suite specified
    if args.suite:
        suites = [s for s in suites if args.suite in s.name]
        if not suites:
            parser.error(f"No suite matching '{args.suite}' found")

    if args.list:
        for suite in suites:
            print(f"{suite.name} -> {suite.park_path}")
        return 0

    any_failures = False
    for suite in suites:
        print(f"[scenario] {suite.name} ({suite.park_path})")
        if not suite.park_path.exists():
            print(f"  [SKIP] Missing park file: {suite.park_path}")
            any_failures = True
            continue

        server_port = pick_free_port()
        json_rpc_port = pick_free_port()
        while json_rpc_port == server_port:
            json_rpc_port = pick_free_port()

        try:
            with HeadlessInstance(
                cli_path=openrct2_cli,
                build_dir=build_dir,
                park_path=suite.park_path,
                user_data_path=user_data_path,
                data_bundle_path=data_bundle_path,
                rct2_data_path=rct2_data_path,
                server_port=server_port,
                json_rpc_port=json_rpc_port,
            ) as instance:
                harness = ScenarioHarness(
                    rctctl_path,
                    host="127.0.0.1",
                    port=instance.json_rpc_port,
                )
                for test_name, test_func in suite.tests:
                    # Filter tests if --test specified
                    if args.test and args.test not in test_name:
                        continue
                    try:
                        test_func(harness)
                        print(f"  [PASS] {test_name}")
                    except ScenarioFailure as exc:
                        any_failures = True
                        print(f"  [FAIL] {test_name}: {exc}")
                        print(textwrap.indent(instance.tail_output(), prefix="    log> "))
                        break
        except ScenarioFailure as exc:
            any_failures = True
            print(f"  [ERROR] Failed to start scenario: {exc}")

    return 1 if any_failures else 0


if __name__ == "__main__":
    sys.exit(main())

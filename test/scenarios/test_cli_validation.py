#!/usr/bin/env python3
"""
CLI validation tests for rctctl.

Tests that error messages and help text are helpful without asserting exact wording.
These tests focus on:
  - Presence of error messages (no silent failures)
  - Semantic quality (not just "error" or stack traces)
  - Recovery guidance (mentions flags, suggests --help, lists valid values)
  - Error categorization (Invalid/Blocked/Internal prefixes)
  - No RPC implementation details in user-facing output

Error Category System:
  - "Invalid:" = CLI misuse (wrong args, types, missing flags) - correctable by user
  - "Blocked:" = Game state prevents action (insufficient funds, land not owned, etc.)
  - "Internal:" = System/connection failures (can't connect, RPC protocol errors)

These tests do NOT require a running OpenRCT2 instance and can run standalone.
Note: "Blocked:" prefix testing requires integration tests with a live game server.
"""

from dataclasses import dataclass
from typing import Optional, List
import subprocess
import re
import sys
from pathlib import Path


@dataclass
class ErrorQuality:
    """Metrics for evaluating error message quality"""
    has_message: bool          # Non-empty stderr
    is_semantic: bool          # Not just "error" or stack trace
    mentions_flag: bool        # References the problematic flag/arg
    suggests_help: bool        # Points to --help or valid values
    exit_code_nonzero: bool    # Proper error exit code

    @property
    def is_helpful(self) -> bool:
        """An error is 'helpful' if it's semantic AND suggests recovery"""
        return (self.has_message and
                self.is_semantic and
                (self.mentions_flag or self.suggests_help) and
                self.exit_code_nonzero)


def run_rctctl_expecting_error(
    args: List[str],
    rctctl_path: str,
    host: str = "127.0.0.1",
    port: int = 9876
) -> subprocess.CompletedProcess:
    """Run rctctl command expected to fail, return full result"""
    cmd = [
        rctctl_path,
        "--host", host,
        "--port", str(port),
        *args
    ]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=5)


def evaluate_error_quality(
    result: subprocess.CompletedProcess,
    expected_flag: Optional[str] = None
) -> ErrorQuality:
    """
    Evaluate if an error message is helpful without checking exact text.

    Args:
        result: The subprocess result from rctctl
        expected_flag: Optional flag name we expect mentioned (e.g., "--id")
    """
    stderr = result.stderr.strip()
    stdout = result.stdout.strip()
    combined = f"{stderr}\n{stdout}"

    # Has message: Non-empty output
    has_message = len(combined) > 0

    # Is semantic: Not just generic error strings or stack traces
    generic_only = re.match(r'^(error|fail|exception)\.?$', combined.lower())
    has_stack_trace = 'Traceback' in combined or 'std::exception' in combined
    is_semantic = has_message and not generic_only and not has_stack_trace

    # Mentions the problematic flag/argument
    mentions_flag = False
    if expected_flag:
        # Look for the flag name or parameter name (strip leading dashes for flexibility)
        flag_name = expected_flag.lstrip('-')
        mentions_flag = flag_name in combined.lower()

    # Suggests help: Mentions --help, valid values, or examples
    help_indicators = [
        '--help',
        'try:',
        'valid values',
        'expected',
        'required',
        'available',
        'usage:',
        'example:',
        'see --help',
        'must provide',
        'allowed values'
    ]
    suggests_help = any(indicator in combined.lower() for indicator in help_indicators)

    return ErrorQuality(
        has_message=has_message,
        is_semantic=is_semantic,
        mentions_flag=mentions_flag,
        suggests_help=suggests_help,
        exit_code_nonzero=result.returncode != 0
    )


def assert_error_is_helpful(
    result: subprocess.CompletedProcess,
    context: str,
    expected_flag: Optional[str] = None
):
    """
    Assert that an error is helpful, with clear failure messages.

    Args:
        result: The subprocess result
        context: Description for error messages (e.g., "missing --id flag")
        expected_flag: Optional flag we expect to be mentioned
    """
    quality = evaluate_error_quality(result, expected_flag)

    if not quality.has_message:
        raise AssertionError(
            f"[{context}] No error message produced (silent failure)\n"
            f"Exit code: {result.returncode}"
        )

    if not quality.exit_code_nonzero:
        raise AssertionError(
            f"[{context}] Exit code was 0 despite error\n"
            f"Output: {result.stderr or result.stdout}"
        )

    if not quality.is_semantic:
        raise AssertionError(
            f"[{context}] Error message is not semantic (generic or stack trace)\n"
            f"Output: {result.stderr or result.stdout}"
        )

    if not quality.is_helpful:
        raise AssertionError(
            f"[{context}] Error message lacks guidance\n"
            f"  Has message: {quality.has_message}\n"
            f"  Is semantic: {quality.is_semantic}\n"
            f"  Mentions flag: {quality.mentions_flag} (expected: {expected_flag})\n"
            f"  Suggests help: {quality.suggests_help}\n"
            f"Output:\n{result.stderr or result.stdout}"
        )


class RctctlValidationTests:
    """Test suite for CLI validation and error guidance"""

    def __init__(self, rctctl_path: str, host: str = "127.0.0.1", port: int = 9876):
        self.rctctl_path = rctctl_path
        self.host = host
        self.port = port

    def run_expecting_error(self, *args: str) -> subprocess.CompletedProcess:
        return run_rctctl_expecting_error(
            list(args),
            self.rctctl_path,
            self.host,
            self.port
        )

    # -------------------------------------------------------------------------
    # Missing Required Arguments
    # -------------------------------------------------------------------------

    def test_rides_status_missing_identifier(self):
        """rides get requires --id or --name"""
        result = self.run_expecting_error("rides", "get")
        assert_error_is_helpful(
            result,
            "rides status without identifier",
            expected_flag="--id"
        )

    def test_construction_land_raise_missing_coordinates(self):
        """construction land raise requires --x and --y"""
        result = self.run_expecting_error("construction", "land", "raise")
        assert_error_is_helpful(
            result,
            "land raise without coordinates",
            expected_flag="--x"
        )

    def test_staff_get_missing_id(self):
        """staff get requires --id"""
        result = self.run_expecting_error("staff", "get")
        assert_error_is_helpful(
            result,
            "staff get without id",
            expected_flag="--id"
        )

    def test_guests_get_missing_id(self):
        """guests get requires --id"""
        result = self.run_expecting_error("guests", "get")
        assert_error_is_helpful(
            result,
            "guests get without id",
            expected_flag="--id"
        )

    def test_guests_list_invalid_after_format(self):
        """guests list with non-numeric --after"""
        result = self.run_expecting_error("guests", "list", "--after", "not-a-number")
        assert_error_is_helpful(
            result,
            "guests list with invalid after format",
            expected_flag="--after"
        )

    def test_guests_list_invalid_limit_format(self):
        """guests list with non-numeric --limit"""
        result = self.run_expecting_error("guests", "list", "--limit", "not-a-number")
        assert_error_is_helpful(
            result,
            "guests list with invalid limit format",
            expected_flag="--limit"
        )

    def test_guests_thoughts_invalid_offset_format(self):
        """guests thoughts with non-numeric --offset"""
        result = self.run_expecting_error("guests", "thoughts", "--offset", "not-a-number")
        assert_error_is_helpful(
            result,
            "guests thoughts with invalid offset format",
            expected_flag="--offset"
        )

    def test_guests_thoughts_invalid_limit_format(self):
        """guests thoughts with non-numeric --limit"""
        result = self.run_expecting_error("guests", "thoughts", "--limit", "not-a-number")
        assert_error_is_helpful(
            result,
            "guests thoughts with invalid limit format",
            expected_flag="--limit"
        )

    def test_guests_thoughts_invalid_guest_limit_format(self):
        """guests thoughts with non-numeric --guest-limit"""
        result = self.run_expecting_error("guests", "thoughts", "--guest-limit", "not-a-number")
        assert_error_is_helpful(
            result,
            "guests thoughts with invalid guest-limit format",
            expected_flag="--guest-limit"
        )

    def test_map_tile_missing_coordinates(self):
        """map tile requires --x and --y"""
        result = self.run_expecting_error("map", "tile")
        assert_error_is_helpful(
            result,
            "map tile without coordinates",
            expected_flag="--x"
        )

    # NOTE: map area commands intentionally allow --x/--y to be optional
    # (defaults to park center), so no validation tests for missing coordinates

    def test_park_price_set_missing_value(self):
        """park price set requires --value"""
        result = self.run_expecting_error("park", "price", "set")
        assert_error_is_helpful(
            result,
            "park price set without value",
            expected_flag="--value"
        )

    def test_rides_price_set_missing_value(self):
        """rides price set requires --value and identifier"""
        result = self.run_expecting_error("rides", "price", "set")
        assert_error_is_helpful(
            result,
            "rides price set without args",
            expected_flag="--value"
        )

    def test_rides_tune_missing_identifier(self):
        """rides tune requires --id or --name"""
        result = self.run_expecting_error("rides", "tune")
        assert_error_is_helpful(
            result,
            "rides tune without identifier",
            expected_flag="--id"
        )

    def test_rides_tune_invalid_wait_for_load(self):
        """rides tune with invalid wait-for-load value should error with valid options"""
        result = self.run_expecting_error(
            "rides", "tune", "--id", "0", "--wait-for-load", "invalid"
        )
        assert_error_is_helpful(
            result,
            "rides tune with invalid wait-for-load value",
            expected_flag="wait-for-load"
        )
        # Should mention valid values
        combined = f"{result.stderr}\n{result.stdout}".lower()
        has_valid_values = any(v in combined for v in ["any", "quarter", "half", "full"])
        assert has_valid_values, \
            f"Invalid wait-for-load error should list valid values, got: {result.stderr}"

    def test_rides_refurbish_missing_identifier(self):
        """rides refurbish requires --id or --name"""
        result = self.run_expecting_error("rides", "refurbish")
        assert_error_is_helpful(
            result,
            "rides refurbish without identifier",
            expected_flag="--id"
        )

    def test_rides_demolish_missing_identifier(self):
        """rides demolish requires --id or --name"""
        result = self.run_expecting_error("rides", "demolish")
        assert_error_is_helpful(
            result,
            "rides demolish without identifier",
            expected_flag="--id"
        )

    # -------------------------------------------------------------------------
    # Ride Theme Commands
    # -------------------------------------------------------------------------

    def test_rides_theme_get_missing_identifier(self):
        """rides theme get requires --id or --name"""
        result = self.run_expecting_error("rides", "theme", "get")
        assert_error_is_helpful(
            result,
            "rides theme get without identifier",
            expected_flag="--id"
        )

    def test_rides_theme_track_set_missing_identifier(self):
        """rides theme track set requires --id or --name"""
        result = self.run_expecting_error("rides", "theme", "track", "set")
        assert_error_is_helpful(
            result,
            "rides theme track set without identifier",
            expected_flag="--id"
        )

    def test_rides_theme_track_set_missing_color(self):
        """rides theme track set requires at least one color flag"""
        result = self.run_expecting_error("rides", "theme", "track", "set", "--id", "0")
        assert_error_is_helpful(
            result,
            "rides theme track set without color flag",
            expected_flag="main"
        )
        # Should mention valid color flags
        combined = f"{result.stderr}\n{result.stdout}".lower()
        has_color_mention = any(c in combined for c in ["main", "additional", "supports"])
        assert has_color_mention, \
            f"Missing color flag error should mention valid colors, got: {result.stderr}"

    def test_rides_theme_vehicle_set_missing_identifier(self):
        """rides theme vehicle set requires --id or --name"""
        result = self.run_expecting_error("rides", "theme", "vehicle", "set")
        assert_error_is_helpful(
            result,
            "rides theme vehicle set without identifier",
            expected_flag="--id"
        )

    def test_rides_theme_vehicle_set_missing_color(self):
        """rides theme vehicle set requires at least one color flag"""
        result = self.run_expecting_error("rides", "theme", "vehicle", "set", "--id", "0")
        assert_error_is_helpful(
            result,
            "rides theme vehicle set without color flag",
            expected_flag="body"
        )
        # Should mention valid color flags
        combined = f"{result.stderr}\n{result.stdout}".lower()
        has_color_mention = any(c in combined for c in ["body", "trim", "tertiary"])
        assert has_color_mention, \
            f"Missing color flag error should mention valid colors, got: {result.stderr}"

    def test_rides_theme_vehicle_mode_missing_identifier(self):
        """rides theme vehicle mode requires --id or --name"""
        result = self.run_expecting_error("rides", "theme", "vehicle", "mode")
        assert_error_is_helpful(
            result,
            "rides theme vehicle mode without identifier",
            expected_flag="--id"
        )

    def test_rides_theme_vehicle_mode_missing_mode(self):
        """rides theme vehicle mode requires --mode"""
        result = self.run_expecting_error("rides", "theme", "vehicle", "mode", "--id", "0")
        assert_error_is_helpful(
            result,
            "rides theme vehicle mode without mode flag",
            expected_flag="--mode"
        )
        # Should mention valid modes
        combined = f"{result.stderr}\n{result.stdout}".lower()
        has_mode_mention = any(m in combined for m in ["same", "per-train", "per_train", "pertrain"])
        assert has_mode_mention, \
            f"Missing mode error should mention valid modes, got: {result.stderr}"

    def test_rides_theme_vehicle_mode_invalid_mode(self):
        """rides theme vehicle mode with invalid mode value"""
        result = self.run_expecting_error(
            "rides", "theme", "vehicle", "mode", "--id", "0", "--mode", "invalid"
        )
        assert_error_is_helpful(
            result,
            "rides theme vehicle mode with invalid mode",
            expected_flag="--mode"
        )
        # Should mention valid modes
        combined = f"{result.stderr}\n{result.stdout}".lower()
        has_mode_mention = any(m in combined for m in ["same", "per-train", "per_train", "pertrain"])
        assert has_mode_mention, \
            f"Invalid mode error should list valid modes, got: {result.stderr}"

    def test_rides_theme_entrance_set_missing_identifier(self):
        """rides theme entrance set requires --id or --name"""
        result = self.run_expecting_error("rides", "theme", "entrance", "set")
        assert_error_is_helpful(
            result,
            "rides theme entrance set without identifier",
            expected_flag="--id"
        )

    def test_rides_theme_entrance_set_missing_style(self):
        """rides theme entrance set requires --style"""
        result = self.run_expecting_error("rides", "theme", "entrance", "set", "--id", "0")
        assert_error_is_helpful(
            result,
            "rides theme entrance set without style",
            expected_flag="--style"
        )

    def test_staff_patrol_missing_id(self):
        """staff patrol requires --id"""
        result = self.run_expecting_error("staff", "patrol")
        assert_error_is_helpful(
            result,
            "staff patrol without id",
            expected_flag="--id"
        )

    def test_guests_drop_missing_id(self):
        """guests drop requires --id"""
        result = self.run_expecting_error("guests", "drop")
        assert_error_is_helpful(
            result,
            "guests drop without id",
            expected_flag="--id"
        )

    def test_guests_pickup_missing_id(self):
        """guests pickup requires --id"""
        result = self.run_expecting_error("guests", "pickup")
        assert_error_is_helpful(
            result,
            "guests pickup without id",
            expected_flag="--id"
        )

    def test_guests_place_missing_id(self):
        """guests place requires --id"""
        result = self.run_expecting_error("guests", "place")
        assert_error_is_helpful(
            result,
            "guests place without id",
            expected_flag="--id"
        )

    def test_guests_place_missing_coordinates(self):
        """guests place requires --x and --y"""
        result = self.run_expecting_error("guests", "place", "--id", "123")
        assert_error_is_helpful(
            result,
            "guests place without coordinates",
            expected_flag="--x"
        )

    def test_trees_place_missing_coordinates(self):
        """trees place requires --x, --y, and --tree-id"""
        result = self.run_expecting_error("trees", "place")
        assert_error_is_helpful(
            result,
            "trees place without coordinates",
            expected_flag="--x"
        )

    def test_scenery_place_missing_coordinates(self):
        """scenery place requires --x, --y, and --scenery-id"""
        result = self.run_expecting_error("scenery", "place")
        assert_error_is_helpful(
            result,
            "scenery place without coordinates",
            expected_flag="--x"
        )

    def test_scenery_place_missing_scenery_id(self):
        """scenery place with coordinates but missing --scenery-id"""
        result = self.run_expecting_error("scenery", "place", "--x", "50", "--y", "50")
        assert_error_is_helpful(
            result,
            "scenery place without scenery-id",
            expected_flag="scenery"
        )

    def test_construction_scenery_clear_missing_coordinates(self):
        """construction scenery clear requires --x and --y"""
        result = self.run_expecting_error("construction", "scenery", "clear", "--small")
        assert_error_is_helpful(
            result,
            "scenery clear without coordinates",
            expected_flag="--x"
        )

    def test_construction_scenery_clear_missing_filter_flag(self):
        """construction scenery clear requires at least one filter flag"""
        result = self.run_expecting_error("construction", "scenery", "clear", "--x", "50", "--y", "50")
        assert_error_is_helpful(
            result,
            "scenery clear without filter flag",
            expected_flag="small"
        )
        # Should mention valid filter flags
        combined = f"{result.stderr}\n{result.stdout}".lower()
        has_filter_mention = any(f in combined for f in ["small", "large", "paths"])
        assert has_filter_mention, \
            f"Missing filter flag error should mention valid filters, got: {result.stderr}"

    def test_construction_scenery_clear_invalid_size(self):
        """construction scenery clear with invalid size"""
        result = self.run_expecting_error(
            "construction", "scenery", "clear", "--x", "50", "--y", "50", "--small", "--size", "0"
        )
        assert_error_is_helpful(
            result,
            "scenery clear with invalid size",
            expected_flag="size"
        )

    def test_scenery_place_invalid_quadrant(self):
        """scenery place with invalid quadrant value"""
        result = self.run_expecting_error(
            "scenery", "place", "--x", "50", "--y", "50",
            "--scenery-id", "test", "--quadrant", "5"
        )
        assert_error_is_helpful(
            result,
            "scenery place with invalid quadrant",
            expected_flag="quadrant"
        )

    def test_scenery_place_invalid_colour(self):
        """scenery place with invalid colour value"""
        result = self.run_expecting_error(
            "scenery", "place", "--x", "50", "--y", "50",
            "--scenery-id", "test", "--primary-colour", "99"
        )
        assert_error_is_helpful(
            result,
            "scenery place with invalid colour",
            expected_flag="colour"
        )

    def test_path_items_place_missing_coordinates(self):
        """path-items place requires --x, --y, and --item-id"""
        result = self.run_expecting_error("path-items", "place")
        assert_error_is_helpful(
            result,
            "path-items place without coordinates",
            expected_flag="--x"
        )

    def test_path_items_place_missing_item_id(self):
        """path-items place with coordinates but missing --item-id"""
        result = self.run_expecting_error("path-items", "place", "--x", "50", "--y", "50")
        assert_error_is_helpful(
            result,
            "path-items place without item-id",
            expected_flag="item"
        )

    def test_path_items_remove_missing_coordinates(self):
        """path-items remove requires --x and --y"""
        result = self.run_expecting_error("path-items", "remove")
        assert_error_is_helpful(
            result,
            "path-items remove without coordinates",
            expected_flag="--x"
        )

    def test_path_items_remove_negative_x(self):
        """path-items remove with negative --x"""
        result = self.run_expecting_error("path-items", "remove", "--x", "-5", "--y", "50")
        assert_error_is_helpful(
            result,
            "path-items remove with negative --x",
            expected_flag="--x"
        )

    def test_paths_remove_missing_coordinates(self):
        """paths remove requires --x and --y"""
        result = self.run_expecting_error("paths", "remove")
        assert_error_is_helpful(
            result,
            "paths remove without coordinates",
            expected_flag="--x"
        )

    def test_paths_remove_negative_x(self):
        """paths remove with negative --x"""
        result = self.run_expecting_error("paths", "remove", "--x", "-5", "--y", "50")
        assert_error_is_helpful(
            result,
            "paths remove with negative --x",
            expected_flag="--x"
        )

    def test_paths_place_slope_without_z(self):
        """paths place with --slope but no --z should error"""
        result = self.run_expecting_error(
            "paths", "place", "--x", "50", "--y", "50", "--surface", "tarmac", "--slope", "north"
        )
        assert_error_is_helpful(
            result,
            "paths place --slope without --z",
            expected_flag="--slope"
        )
        # Should mention that --z is required
        combined = f"{result.stderr}\n{result.stdout}".lower()
        assert "--z" in combined, f"Error should mention --z requirement, got: {result.stderr}"

    def test_paths_place_invalid_slope_direction(self):
        """paths place with invalid --slope direction should error"""
        result = self.run_expecting_error(
            "paths", "place", "--x", "50", "--y", "50", "--surface", "tarmac", "--z", "128", "--slope", "diagonal"
        )
        assert_error_is_helpful(
            result,
            "paths place with invalid --slope direction",
            expected_flag="--slope"
        )
        # Should mention valid directions
        combined = f"{result.stderr}\n{result.stdout}".lower()
        assert any(d in combined for d in ["north", "south", "east", "west"]), \
            f"Error should mention valid slope directions, got: {result.stderr}"

    def test_path_items_list_invalid_type(self):
        """path-items list with invalid --type should error with valid options"""
        result = self.run_expecting_error("path-items", "list", "--type", "invalid")
        assert_error_is_helpful(
            result,
            "path-items list with invalid type",
            expected_flag="--type"
        )
        # Should mention valid types
        combined = f"{result.stderr}\n{result.stdout}".lower()
        assert any(t in combined for t in ["bench", "bin", "lamp"]), \
            f"Invalid type error should list valid types, got: {result.stderr}"

    def test_path_items_list_invalid_limit(self):
        """path-items list with invalid --limit should error"""
        result = self.run_expecting_error("path-items", "list", "--limit", "0")
        assert_error_is_helpful(
            result,
            "path-items list with invalid limit",
            expected_flag="--limit"
        )

    def test_path_items_list_invalid_order(self):
        """path-items list with invalid --order should error with valid options"""
        result = self.run_expecting_error("path-items", "list", "--order", "invalid")
        assert_error_is_helpful(
            result,
            "path-items list with invalid order",
            expected_flag="--order"
        )
        # Should mention valid order fields
        combined = f"{result.stderr}\n{result.stdout}".lower()
        assert any(f in combined for f in ["type", "broken", "x", "y"]), \
            f"Invalid order error should list valid fields, got: {result.stderr}"

    def test_path_items_list_invalid_direction(self):
        """path-items list with invalid --direction should error with valid options"""
        result = self.run_expecting_error("path-items", "list", "--direction", "invalid")
        assert_error_is_helpful(
            result,
            "path-items list with invalid direction",
            expected_flag="--direction"
        )
        # Should mention valid directions
        combined = f"{result.stderr}\n{result.stdout}".lower()
        assert any(d in combined for d in ["asc", "desc"]), \
            f"Invalid direction error should list valid directions, got: {result.stderr}"

    # -------------------------------------------------------------------------
    # Invalid Argument Values
    # -------------------------------------------------------------------------

    def test_rides_status_invalid_id_format(self):
        """rides get with non-numeric ID"""
        result = self.run_expecting_error("rides", "get", "--id", "not-a-number")
        assert_error_is_helpful(
            result,
            "rides status with invalid ID format",
            expected_flag="--id"
        )

    def test_park_price_set_negative_value(self):
        """park price set with negative value should error or warn"""
        result = self.run_expecting_error("park", "price", "set", "--value", "-10.00")
        # May be caught at RPC level; if it errors, should be helpful
        if result.returncode != 0:
            quality = evaluate_error_quality(result, "--value")
            assert quality.is_semantic, \
                f"Negative price error should be semantic, got: {result.stderr}"

    def test_staff_hire_invalid_role(self):
        """staff hire with invalid role"""
        result = self.run_expecting_error("staff", "hire", "--role", "invalid-role")
        # If validation exists, should be helpful
        if result.returncode != 0:
            quality = evaluate_error_quality(result, "--role")
            assert quality.is_semantic, \
                f"Invalid role error should be semantic, got: {result.stderr}"

    def test_port_out_of_range(self):
        """--port with out-of-range value"""
        result = subprocess.run(
            [self.rctctl_path, "--port", "99999", "park", "status"],
            capture_output=True,
            text=True,
            timeout=5
        )
        # Port validation may vary; if it errors, should be helpful
        if result.returncode != 0:
            quality = evaluate_error_quality(result, "--port")
            assert quality.is_semantic, \
                f"Invalid port error should be semantic, got: {result.stderr}"

    # -------------------------------------------------------------------------
    # Unknown Commands/Flags
    # -------------------------------------------------------------------------

    def test_unknown_resource(self):
        """Unknown resource name"""
        result = self.run_expecting_error("invalid-resource", "status")
        quality = evaluate_error_quality(result)
        assert quality.is_helpful, \
            f"Unknown resource error should be helpful, got: {result.stderr}"

    def test_unknown_verb(self):
        """Unknown verb for known resource"""
        result = self.run_expecting_error("park", "invalid-verb")
        quality = evaluate_error_quality(result)
        assert quality.is_helpful, \
            f"Unknown verb error should be helpful, got: {result.stderr}"

    def test_unknown_subcommand(self):
        """Unknown subcommand"""
        result = self.run_expecting_error("construction", "land", "invalid-action")
        quality = evaluate_error_quality(result)
        assert quality.is_helpful, \
            f"Unknown subcommand error should be helpful, got: {result.stderr}"

    # -------------------------------------------------------------------------
    # Help Text Availability
    # -------------------------------------------------------------------------

    def test_all_commands_have_help(self):
        """Every registered command should respond to --help"""
        commands_to_test = [
            ["park", "status"],
            ["park", "guests"],
            ["park", "warnings"],
            ["park", "open"],
            ["park", "close"],
            ["park", "price"],
            ["park", "price", "set"],
            ["rides", "list"],
            ["rides", "get"],
            ["rides", "open"],
            ["rides", "close"],
            ["rides", "price"],
            ["rides", "tune"],
            ["rides", "catalog"],
            ["rides", "theme", "get"],
            ["rides", "theme", "track", "set"],
            ["rides", "theme", "vehicle", "set"],
            ["rides", "theme", "vehicle", "mode"],
            ["rides", "theme", "entrance", "set"],
            ["rides", "theme", "entrance", "list"],
            ["rides", "theme", "colors"],
            ["staff", "list"],
            ["staff", "get"],
            ["staff", "hire"],
            ["staff", "fire"],
            ["staff", "patrol"],
            ["guests", "list"],
            ["guests", "get"],
            ["guests", "thoughts"],
            ["guests", "moods"],
            ["guests", "pickup"],
            ["guests", "place"],
            ["guests", "drop"],
            ["map", "status"],
            ["map", "tile"],
            ["map", "area"],
            ["map", "area", "paths"],
            ["map", "area", "rides"],
            ["map", "area", "ownership"],
            ["map", "area", "scenery"],
            ["map", "area", "water"],
            ["map", "area", "shops"],
            ["map", "ownership"],
            ["map", "heatmap", "guests"],
            ["map", "scan", "development"],
            ["map", "scan", "guests"],
            ["construction", "land", "raise"],
            ["construction", "land", "lower"],
            ["construction", "water", "raise"],
            ["construction", "water", "lower"],
            ["construction", "scenery", "clear"],
            ["trees", "catalog"],
            ["trees", "place"],
            ["scenery", "catalog"],
            ["scenery", "place"],
            ["path-items", "catalog"],
            ["path-items", "place"],
            ["path-items", "remove"],
            ["paths", "place"],
            ["paths", "catalog"],
            ["paths", "remove"],
            ["path-items", "list"],
            ["weather", "status"],
            ["weather", "forecast"],
            ["research", "status"],
            ["research", "set"],
            ["marketing", "status"],
            ["marketing", "launch"],
            ["finance", "status"],
            ["news", "list"],
            ["news", "history"],
            ["awards", "list"],
            ["awards", "history"],
            ["entrances", "list"],
        ]

        failures = []
        for cmd in commands_to_test:
            result = subprocess.run(
                [self.rctctl_path, *cmd, "--help"],
                capture_output=True,
                text=True,
                timeout=5
            )

            # Help should succeed
            if result.returncode != 0:
                failures.append(f"{' '.join(cmd)}: exit code {result.returncode}")
                continue

            output = result.stdout + result.stderr

            # Help should contain useful info
            if len(output) < 30:  # Minimum threshold for useful help
                failures.append(f"{' '.join(cmd)}: help too short ({len(output)} chars)")
                continue

            # Help should mention the resource or command
            cmd_words = set(word.lower() for word in cmd)
            output_lower = output.lower()
            if not any(word in output_lower for word in cmd_words):
                failures.append(f"{' '.join(cmd)}: doesn't mention command words")

        if failures:
            raise AssertionError(
                f"Help text issues:\n  " + "\n  ".join(failures)
            )

    # -------------------------------------------------------------------------
    # Multi-level Help
    # -------------------------------------------------------------------------

    def test_resource_level_help(self):
        """Resources should have overview help (rctctl park --help)"""
        resources = ["park", "rides", "staff", "guests", "map", "construction",
                     "trees", "scenery", "path-items", "paths", "weather", "research", "marketing",
                     "finance", "news", "awards", "entrances", "shops"]

        failures = []
        for resource in resources:
            result = subprocess.run(
                [self.rctctl_path, resource, "--help"],
                capture_output=True,
                text=True,
                timeout=5
            )

            if result.returncode != 0:
                failures.append(f"{resource}: help failed with exit code {result.returncode}")
                continue

            output = result.stdout + result.stderr
            if len(output) < 50:
                failures.append(f"{resource}: help too brief ({len(output)} chars)")
                continue

            # Should list available verbs/commands or describe the resource
            has_content = (
                'status' in output.lower() or
                'list' in output.lower() or
                'commands' in output.lower() or
                'available' in output.lower()
            )
            if not has_content:
                failures.append(f"{resource}: help doesn't list verbs or commands")

        if failures:
            raise AssertionError(
                f"Resource-level help issues:\n  " + "\n  ".join(failures)
            )

    def test_top_level_help(self):
        """Top-level help should list resources"""
        result = subprocess.run(
            [self.rctctl_path, "--help"],
            capture_output=True,
            text=True,
            timeout=5
        )

        assert result.returncode == 0, "Top-level --help should succeed"

        output = result.stdout + result.stderr
        assert len(output) > 100, "Top-level help should be substantial"

        # Should mention key resources
        key_resources = ["park", "rides", "staff", "guests", "map"]
        output_lower = output.lower()
        mentioned = sum(1 for res in key_resources if res in output_lower)

        assert mentioned >= 3, \
            f"Top-level help should mention at least 3 key resources, found {mentioned}"

    # -------------------------------------------------------------------------
    # Error Message Quality: No Implementation Details
    # -------------------------------------------------------------------------

    def test_no_rpc_implementation_details(self):
        """Error messages must not expose RPC protocol details"""
        forbidden_patterns = [
            r'RPC error',
            r'-32\d{3}',  # RPC error codes like -32010, -32602
            r'json.?rpc',
            r'JSON-RPC',
        ]

        # Test various CLI-level error scenarios
        test_cases = [
            ["rides", "get"],  # Missing required arg
            ["invalid-resource", "status"],  # Unknown command
            ["rides", "get", "--id", "not-a-number"],  # Type error
            ["park", "invalid-verb"],  # Unknown verb
            ["construction", "land", "raise"],  # Missing coordinates
            ["staff", "get"],  # Missing id
        ]

        failures = []
        for args in test_cases:
            result = self.run_expecting_error(*args)
            output = result.stderr + result.stdout

            for pattern in forbidden_patterns:
                if re.search(pattern, output, re.IGNORECASE):
                    failures.append(
                        f"  {' '.join(args)}: Found forbidden pattern '{pattern}'\n"
                        f"    Output: {output.strip()}"
                    )

        if failures:
            raise AssertionError(
                "RPC implementation details leaked into user-facing errors:\n" + "\n".join(failures)
            )

    def test_cli_errors_have_invalid_prefix(self):
        """All CLI validation errors should consistently use 'Invalid:' prefix"""
        # These are all CLI-level errors that should have Invalid: prefix
        cli_error_commands = [
            (["rides", "get"], "missing identifier"),
            (["construction", "land", "raise"], "missing coordinates"),
            (["staff", "get"], "missing id"),
            (["map", "tile"], "missing coordinates"),
            (["rides", "get", "--id", "not-a-number"], "type error"),
            (["invalid-resource", "status"], "unknown resource"),
            (["park", "invalid-verb"], "unknown verb"),
            (["construction", "land", "invalid-action"], "unknown subcommand"),
            (["rides", "tune", "--id", "1"], "missing tuning flag"),
            (["staff", "orders", "--id", "1"], "missing order toggle"),
            (["--host"], "missing flag value"),
        ]

        failures = []
        for args, description in cli_error_commands:
            result = self.run_expecting_error(*args)
            if result.returncode != 0:
                # Check that stderr starts with "rctctl: Invalid:"
                if not result.stderr.startswith("rctctl: Invalid:"):
                    failures.append(
                        f"  {' '.join(args)} ({description}):\n"
                        f"    Expected to start with 'rctctl: Invalid:'\n"
                        f"    Got: {result.stderr.strip()}"
                    )

        if failures:
            raise AssertionError(
                "CLI errors missing 'Invalid:' prefix:\n" + "\n".join(failures)
            )

    def test_connection_errors_have_internal_prefix(self):
        """Connection failures should consistently use 'Internal:' prefix"""
        # Test connection failure scenarios that fail fast
        connection_error_tests = [
            {
                "host": "127.0.0.1",
                "port": 1,  # Port likely not in use, fast connection refused
                "description": "connection refused (port 1)"
            },
            {
                "host": "0.0.0.0",
                "port": 1,
                "description": "invalid connection target"
            },
        ]

        failures = []
        for test_case in connection_error_tests:
            try:
                result = subprocess.run(
                    [
                        self.rctctl_path,
                        "--host", test_case["host"],
                        "--port", str(test_case["port"]),
                        "park", "status"
                    ],
                    capture_output=True,
                    text=True,
                    timeout=5  # Should fail fast
                )
            except subprocess.TimeoutExpired:
                # Some connection attempts may hang, skip those
                print(f"  [SKIP] {test_case['description']}: Connection timed out",
                      file=sys.stderr)
                continue

            # Should fail with non-zero exit code
            if result.returncode == 0:
                failures.append(
                    f"  {test_case['description']}: Expected failure but got exit code 0"
                )
                continue

            # Check that stderr starts with "rctctl: Internal:"
            if not result.stderr.startswith("rctctl: Internal:"):
                failures.append(
                    f"  {test_case['description']}:\n"
                    f"    Expected to start with 'rctctl: Internal:'\n"
                    f"    Got: {result.stderr.strip()}"
                )

        if not failures and len(connection_error_tests) > 0:
            # At least verify one connection error scenario worked
            # (if all timed out, that's okay - this is platform dependent)
            pass

        if failures:
            raise AssertionError(
                "Connection errors missing 'Internal:' prefix:\n" + "\n".join(failures)
            )

    def test_error_categorization_is_comprehensive(self):
        """All errors should have one of the three category prefixes"""
        # Test a variety of error scenarios to ensure everything is categorized
        test_cases = [
            # CLI validation errors -> Invalid:
            (["rides", "get"], "Invalid:"),
            (["rides", "get", "--id", "abc"], "Invalid:"),
            (["invalid-command"], "Invalid:"),
            (["--port", "99999", "park", "status"], None),  # May or may not error

            # Connection errors -> Internal:
            (["--host", "0.0.0.0", "--port", "1", "park", "status"], "Internal:"),
        ]

        failures = []
        for args, expected_prefix in test_cases:
            # Build full command
            if "--host" not in args:
                full_args = ["--host", "0.0.0.0", "--port", "1"] + args
            else:
                full_args = args

            result = subprocess.run(
                [self.rctctl_path] + full_args,
                capture_output=True,
                text=True,
                timeout=10
            )

            # Skip if it unexpectedly succeeded
            if result.returncode == 0:
                continue

            # Check if error has a recognized prefix
            stderr = result.stderr.strip()
            has_category = (
                "Invalid:" in stderr or
                "Blocked:" in stderr or
                "Internal:" in stderr
            )

            if not has_category:
                failures.append(
                    f"  {' '.join(args)}:\n"
                    f"    Error has no category prefix (Invalid/Blocked/Internal)\n"
                    f"    Got: {stderr}"
                )
            elif expected_prefix and expected_prefix not in stderr:
                failures.append(
                    f"  {' '.join(args)}:\n"
                    f"    Expected prefix: {expected_prefix}\n"
                    f"    Got: {stderr}"
                )

        if failures:
            raise AssertionError(
                "Errors found without proper categorization:\n" + "\n".join(failures)
            )

    # -------------------------------------------------------------------------
    # Output Format Flags
    # -------------------------------------------------------------------------

    def test_json_output_flag(self):
        """Commands should respect -o json flag"""
        # This test needs a running server, so we'll just verify the flag is recognized
        result = subprocess.run(
            [self.rctctl_path, "--help"],
            capture_output=True,
            text=True,
            timeout=5
        )
        output = result.stdout + result.stderr

        # Help should mention output formatting
        has_output_mention = (
            '-o' in output or
            '--output' in output or
            'json' in output.lower()
        )

        # This is informational rather than strict
        if not has_output_mention:
            print("  [INFO] Top-level help doesn't mention output formatting flags",
                  file=sys.stderr)


def run_all_tests(rctctl_path: Path, verbose: bool = False) -> int:
    """
    Run all validation tests.

    Args:
        rctctl_path: Path to rctctl binary
        verbose: If True, print full error output on failures

    Returns:
        0 if all tests pass, 1 if any fail
    """
    tests = RctctlValidationTests(str(rctctl_path))

    # Get all test methods
    test_methods = [
        (name, getattr(tests, name))
        for name in dir(tests)
        if name.startswith('test_') and callable(getattr(tests, name))
    ]

    failures = []
    passes = 0

    for method_name, method in test_methods:
        try:
            method()
            print(f"  [PASS] {method_name}")
            passes += 1
        except AssertionError as e:
            failures.append((method_name, str(e)))
            print(f"  [FAIL] {method_name}")
            if verbose:
                print(f"    {e}")

    print(f"\n{passes} passed, {len(failures)} failed")

    if failures:
        print("\nFailure details:")
        for name, error in failures:
            print(f"\n{'='*70}")
            print(f"{name}:")
            print(f"{'='*70}")
            print(error)
        return 1

    return 0


def main(argv=None):
    import argparse

    parser = argparse.ArgumentParser(
        description="Run CLI validation tests for rctctl (no game required)"
    )
    parser.add_argument(
        "--rctctl",
        type=Path,
        required=True,
        help="Path to rctctl binary"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show full error details immediately"
    )

    args = parser.parse_args(argv)

    if not args.rctctl.exists():
        print(f"Error: rctctl not found at {args.rctctl}", file=sys.stderr)
        return 1

    return run_all_tests(args.rctctl, verbose=args.verbose)


if __name__ == "__main__":
    sys.exit(main())

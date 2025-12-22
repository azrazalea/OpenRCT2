# Scenario Test Harness

`run_scenarios.py` launches `openrct2-cli` in headless mode with one of the test
parks under `test/tests/testdata/parks`, drives the game through `rctctl`, and
asserts on both JSON payloads and rendered text.

## Prerequisites

- Configure + build the project at least once (run `cmake -S . -B build ...`).
- Ensure `openrct2-cli` and `rctctl` targets have been built (the
  `agent_bundle` target covers both).
- Confirm your local OpenRCT2 user data folder (`~/Library/Application Support/OpenRCT2`
  on macOS) includes a valid `config.ini` with `game_path` pointing at your
  RollerCoaster Tycoon 2 install; the script reads this to find the required
  assets.

## Running

From the repo root:

```bash
test/scenarios/run_scenarios.py
```

Add `--list` to see the registered suites. Use `--user-data-path` or
`--rct2-data-path` if your setup differs from the defaults.
Running via `ctest` executes every suite (currently Ferris Wheel smoke tests, the EverythingPark coverage suite, and the Electric Fields construction suite) sequentially.

"""Pure Vita log parsing and settled-rendering readiness; no transport or I/O."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import NamedTuple
import re

MAIN_MENU_MARKER = "SCENE main_menu run_count="
PROFILE_WINDOW_FRAMES = 120
REQUIRED_STABLE_WINDOWS = 2
MIN_SETTLED_FPS = 50
MAX_SETTLED_P95_US = 25_000
MAX_SETTLED_FRAME_US = 50_000
LOADING_STALL_US = 1_000_000


class LoadStage(str, Enum):
    INITIAL = "initial"
    GAME = "game"
    MAIN_MENU = "main_menu"


class TouchEnd(NamedTuple):
    at_us: int
    x: int
    y: int


@dataclass(frozen=True)
class StableWindow:
    frame: int
    start_us: int
    end_us: int


@dataclass(frozen=True)
class Measurement:
    stage: LoadStage | str
    trigger_us: int
    ready_us: int
    confirmed_us: int
    seconds: float
    stable_windows: list[StableWindow]
    main_menu_marker_seen: bool


def validate_game_log(log: str) -> None:
    if any(marker in log for marker in ("ABORT ", "FATAL", "MEM allocation failed")):
        raise RuntimeError("Game reported a crash or allocation failure; see latest.log")
    if re.search(r"RENDER display=.*\blog_level=[01]\b", log):
        raise RuntimeError("Set log_level=2 in ux0:/data/plague/port.cfg")


TOUCH_END_RECORD = re.compile(r"^INPUT touch action=end at_us=(\d+) id=\d+ x=(\d+) y=(\d+)$")


def parse_log_fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def complete_log_lines(log: str) -> list[str]:
    return [
        re.sub(r"^\[(?:DEBUG|ERROR)\] ", "", line)
        for line in log[: log.rfind("\n") + 1].splitlines()
    ]


def parse_build_header(log: str) -> str | None:
    """Find the wrapper banner even when configuration diagnostics precede it."""
    return next(
        (
            line
            for line in complete_log_lines(log)
            if line.startswith("Plague Inc. Vita / ") and " / build " in line
        ),
        None,
    )


def touch_events(log):
    """Yield complete touch-end records as device time, x and y."""
    for line in complete_log_lines(log):
        match = TOUCH_END_RECORD.fullmatch(line)
        if match:
            yield TouchEnd(*map(int, match.groups()))


def touch_end(log, coordinates):
    for when, x, y in touch_events(log):
        if (x, y) == coordinates:
            return when
    return None


def validate_input(log, expected_coordinates):
    """Reject runs with additional input rather than reporting misleading times."""
    actual = [(x, y) for _, x, y in touch_events(log)]
    expected = list(expected_coordinates)
    if len(actual) > len(expected) or actual != expected[: len(actual)]:
        raise RuntimeError(
            "Unexpected touch input; repeat the measurement without "
            "operating the Vita during the scripted sequence"
        )


def readiness(
    log: str, stage: LoadStage | str, coordinates: tuple[int, int] | None = None
) -> Measurement | None:
    """Require fresh loading evidence and two consecutive settled windows.

    This detects rendering settling after loading, not an engine scene ID.
    Main-menu stages additionally require its PLAGUE_RUN_COUNT update.
    """
    validate_game_log(log)
    trigger = 0 if coordinates is None else touch_end(log, coordinates)
    if trigger is None:
        return None
    saw_menu = saw_upload = saw_load = False
    if stage == "initial":
        saw_load = "PERF startup step=native_run " in log and "FRAME 0" in complete_log_lines(log)
    latest_load_end = trigger
    stable = []
    after_trigger = coordinates is None
    for line in complete_log_lines(log):
        if not after_trigger:
            touch = TOUCH_END_RECORD.fullmatch(line)
            after_trigger = touch is not None and int(touch[1]) == trigger
            continue
        if line.startswith(MAIN_MENU_MARKER):
            saw_menu = True
            stable = []
        if line.startswith("PERF stall "):
            values = parse_log_fields(line)
            if "end_us" not in values:
                raise RuntimeError("PERF timestamps missing; install a current wrapper build")
            if int(values["end_us"]) >= trigger and int(values["total_us"]) >= LOADING_STALL_US:
                saw_load = True
                latest_load_end = int(values["end_us"])
                stable = []
        if not line.startswith("PERF window "):
            continue
        values = parse_log_fields(line)
        if not {"start_us", "end_us"} <= values.keys():
            raise RuntimeError("PERF timestamps missing; install a current wrapper build")
        uploads = int(values["uploads"])
        if uploads:
            saw_upload = True
        settled = (
            saw_load
            and saw_upload
            and (stage == "game" or saw_menu)
            and int(values["start_us"]) >= latest_load_end
            and float(values["fps"]) >= MIN_SETTLED_FPS
            and int(values["p95_us"]) <= MAX_SETTLED_P95_US
            and int(values["max_us"]) <= MAX_SETTLED_FRAME_US
            and uploads == 0
            and int(values["draws"]) > 0
        )
        if not settled:
            stable = []
            continue
        frame = int(values["frame"])
        if stable and frame != stable[-1].frame + PROFILE_WINDOW_FRAMES:
            stable = []
        stable.append(StableWindow(frame, int(values["start_us"]), int(values["end_us"])))
        if len(stable) == REQUIRED_STABLE_WINDOWS:
            return Measurement(
                stage=stage,
                trigger_us=trigger,
                ready_us=stable[0].start_us,
                confirmed_us=stable[-1].end_us,
                seconds=(stable[0].start_us - trigger) / 1_000_000,
                stable_windows=stable,
                main_menu_marker_seen=saw_menu,
            )
    return None

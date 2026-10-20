#!/usr/bin/env python3
"""Measure startup, a new bacteria game, and Save & Quit on a real Vita.

Uses raw 1920x1088 touches, a 200-ms hold, and a five-second pause after
each release. Requires log_level=2 and the timestamped INPUT/PERF log records.
All collected game logs and reports stay in the ignored work/ directory.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
import argparse
import ftplib
import json
import re
import time
import zipfile

from vita_companion import TITLE_ID, VitaCompanion, read_wrapper
from vita_log import (
    LoadStage,
    Measurement,
    parse_build_header,
    readiness,
    touch_end,
    validate_game_log,
    validate_input,
)

ROOT = Path(__file__).resolve().parents[1]
NAVIGATION = (
    ("Spielen", 960, 380),
    ("Neues Spiel", 670, 470),
    ("Bakterie", 420, 400),
    ("Einfach", 460, 540),
    ("Random", 200, 540),
    ("Next", 1560, 540),
)
EXIT_TOUCHES = (
    ("Zahnrad", 50, 50),
    ("Speichern & Beenden", 960, 700),
    ("Ja", 960, 500),
)


HOLD_MS = 200
PAUSE_AFTER_TOUCH_SECONDS = 5
TOUCH_ACK_TIMEOUT_SECONDS = 10
PROGRESS_NOTICE_SECONDS = 20


def _effective_log_level(config: bytes | None) -> int:
    """Read the effective logging setting without changing the Vita configuration."""
    level = 0
    # Split on LF only, like fgets (other whitespace belongs to the value).
    lines = (config or b"").split(b"\n")
    for index, raw in enumerate(lines):
        physical_length = len(raw) + (index < len(lines) - 1)
        # fgets accepts at most 255 bytes, including a terminating newline.
        if physical_length > 255:
            continue
        match = re.fullmatch(rb"[ \t\r\v\f]*log_level=[ \t\r\v\f]*([+-]?[0-9]+)[ \t\r\v\f]*", raw)
        if match:
            value = int(match[1])
            if 0 <= value <= 2:
                level = value
    return level


@dataclass(frozen=True)
class LoadTestConfig:
    ip: str
    command_port: int
    ftp_port: int
    timeout: float
    poll: float
    vpk: Path | None
    output: Path


@dataclass
class TouchAction:
    label: str
    x: int
    y: int
    command_response: str
    touch_end_us: int | None = None
    host_elapsed_seconds: float | None = None


@dataclass(frozen=True)
class TimedMeasurement:
    measurement: Measurement
    host_wait_seconds: float

    def to_dict(self) -> dict:
        return asdict(self.measurement) | {"host_wait_seconds": self.host_wait_seconds}


@dataclass
class LoadTestReport:
    ip: str
    created_at: str
    status: str = "running"
    title: str = TITLE_ID
    coordinates: str = "raw 1920x1088"
    hold_ms: int = HOLD_MS
    pause_after_each_touch_seconds: int = PAUSE_AFTER_TOUCH_SECONDS
    method: str = (
        "Time to start of first of two consecutive settled 120-frame "
        "windows after fresh loading evidence; menus also require "
        "PLAGUE_RUN_COUNT. Initial time starts at process clock zero; "
        "other times start at the touch-end callback. "
        "Window quantization is approximately 2 seconds at 60 FPS."
    )
    touches: list[TouchAction] = field(default_factory=list)
    measurements: list[TimedMeasurement] = field(default_factory=list)
    build_header: str | None = None
    uploaded_eboot_sha256: str | None = None
    launch_sent_at: str | None = None
    launch_response: str | None = None
    error: str | None = None
    cleanup_errors: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        # Preserve the published JSON shape: optional fields appear only when
        # populated, and host timing remains beside the device measurement.
        result = {key: value for key, value in asdict(self).items() if value is not None}
        result["touches"] = [
            {key: value for key, value in asdict(touch).items() if value is not None}
            for touch in self.touches
        ]
        result["measurements"] = [item.to_dict() for item in self.measurements]
        if not self.cleanup_errors:
            del result["cleanup_errors"]
        return result


class LoadTest:
    def __init__(self, config: LoadTestConfig):
        self.config = config
        self.output = config.output.resolve()
        if not self.output.is_relative_to((ROOT / "work").resolve()):
            raise ValueError("Reports must stay under the ignored work/ directory")
        # Validate/read local VPK before any remote stop or log removal. The
        # deployment utility follows the same preflight order.
        self.wrapper_binary = read_wrapper(config.vpk) if config.vpk else None
        self.output.mkdir(parents=True, exist_ok=False)
        self.remote = VitaCompanion(
            config.ip, command_port=config.command_port, ftp_port=config.ftp_port
        )
        self.report = LoadTestReport(
            ip=config.ip, created_at=datetime.now(timezone.utc).isoformat()
        )
        self.save_report()

    def save_report(self):
        (self.output / "report.json").write_text(
            json.dumps(self.report.to_dict(), indent=2, ensure_ascii=False) + "\n"
        )

    def read_and_archive_log(self):
        raw = self.remote.read_log(missing_ok=True)
        (self.output / "latest.log").write_bytes(raw)
        log = raw.decode("utf-8", errors="replace")
        if raw and not self.report.build_header:
            self.report.build_header = parse_build_header(log)
        validate_game_log(log)
        validate_input(log, [(touch.x, touch.y) for touch in self.report.touches])
        return raw

    def tap(self, label, x, y):
        if not (0 <= x < 1920 and 0 <= y < 1088):
            raise ValueError("Touch coordinate outside raw Vita range")
        before = self.read_and_archive_log()
        print(f"Touch {label}: ({x}, {y})", flush=True)
        started = time.monotonic()
        response = self.remote.tap_front(x, y, hold_ms=HOLD_MS)
        event = TouchAction(label, x, y, response)
        self.report.touches.append(event)
        self.save_report()
        time.sleep(PAUSE_AFTER_TOUCH_SECONDS)
        deadline = time.monotonic() + TOUCH_ACK_TIMEOUT_SECONDS
        while True:
            raw = self.read_and_archive_log()
            if len(raw) < len(before):
                raise RuntimeError("Log restarted during touch sequence")
            suffix = raw[len(before) :].decode("utf-8", errors="replace")
            when = touch_end(suffix, (x, y))
            if when is not None:
                event.touch_end_us = when
                event.host_elapsed_seconds = time.monotonic() - started
                self.save_report()
                return len(before)
            if time.monotonic() >= deadline:
                raise RuntimeError(f"{label}: no matching touch-end callback in the game log")
            time.sleep(self.config.poll)

    def wait_ready(self, stage, log_offset_before_trigger=0, coordinates=None):
        stage = stage.value if isinstance(stage, LoadStage) else stage
        started = time.monotonic()
        notice = started
        while time.monotonic() - started < self.config.timeout:
            raw = self.read_and_archive_log()
            if len(raw) < log_offset_before_trigger:
                raise RuntimeError("Log restarted while waiting for " + stage)
            result = readiness(
                raw[log_offset_before_trigger:].decode("utf-8", errors="replace"),
                stage,
                coordinates,
            )
            if result:
                self.report.measurements.append(
                    TimedMeasurement(result, time.monotonic() - started)
                )
                (self.output / (stage + ".log")).write_bytes(raw)
                self.save_report()
                print(f"Ready {stage}: {result.seconds:.3f} s", flush=True)
                return
            now = time.monotonic()
            if now - notice >= PROGRESS_NOTICE_SECONDS:
                print(f"Waiting for {stage}: {now - started:.0f} s", flush=True)
                notice = now
            time.sleep(self.config.poll)
        raise TimeoutError(
            f"{stage}: no completed loading phase followed by two stable windows "
            f"within {self.config.timeout:g} seconds; see latest.log"
        )

    def archive_and_stop(self):
        (self.output / "previous.log").write_bytes(self.remote.read_log(missing_ok=True))
        print(self.remote.stop_application(TITLE_ID), flush=True)
        time.sleep(2)
        frozen = self.remote.read_log(missing_ok=True)
        (self.output / "previous-stopped.log").write_bytes(frozen)
        # Remove only the diagnostic log already archived, never saves/assets.
        if frozen:
            self.remote.remove_log()

    def upload_wrapper(self):
        if self.wrapper_binary is not None:
            self.report.uploaded_eboot_sha256 = self.remote.upload_binary(
                self.wrapper_binary, verify=True
            )

    def launch_and_measure_startup(self):
        self.report.launch_sent_at = datetime.now(timezone.utc).isoformat()
        self.report.launch_response = self.remote.launch(TITLE_ID)
        time.sleep(2)
        self.read_and_archive_log()
        self.save_report()
        self.wait_ready(LoadStage.INITIAL)

    def measure_game_and_return(self):
        for label, x, y in NAVIGATION:
            self.tap(label, x, y)
        log_offset_before_trigger = self.tap("Start", 1660, 1000)
        self.wait_ready(LoadStage.GAME, log_offset_before_trigger, (1660, 1000))
        for label, x, y in EXIT_TOUCHES:
            log_offset_before_trigger = self.tap(label, x, y)
        self.wait_ready(LoadStage.MAIN_MENU, log_offset_before_trigger, EXIT_TOUCHES[-1][1:])

    def cleanup(self, *, release_touch=True):
        # Every cleanup step runs even if another fails. Original run exceptions
        # keep their traceback; cleanup failures are printed AND saved in report.
        actions = []
        if release_touch:
            actions.append(("release touch", self.remote.release_front_touch))
        actions.append(("close FTP", self.remote.close))
        for operation, action in actions:
            try:
                action()
            except Exception as error:
                message = f"{operation}: {error}"
                self.report.cleanup_errors.append(message)
                print("Cleanup failed:", message, flush=True)
        try:
            self.save_report()
        except OSError as error:
            print("Report could not be saved:", error, flush=True)
        print("Report:", self.output / "report.json", flush=True)

    def run(self):
        remote_actions_started = False
        try:
            self.remote.require_installed_app()
            if _effective_log_level(self.remote.read_config()) != 2:
                raise RuntimeError(
                    "Set log_level=2 in ux0:/data/plague/port.cfg before measuring; "
                    "no stop, upload, or log removal was performed"
                )
            remote_actions_started = True
            self.archive_and_stop()
            self.upload_wrapper()
            self.launch_and_measure_startup()
            self.measure_game_and_return()
            self.report.status = "complete"
            print("Load times (device clock; settled rendering criterion):", flush=True)
            for item in self.report.measurements:
                print(f"  {item.measurement.stage}: {item.measurement.seconds:.3f} s", flush=True)
        except BaseException as error:
            self.report.status = "failed"
            self.report.error = str(error)
            raise
        finally:
            self.cleanup(release_touch=remote_actions_started)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", required=True)
    parser.add_argument("--ftp-port", type=int, default=1337)
    parser.add_argument("--command-port", type=int, default=1338)
    parser.add_argument("--timeout", type=float, default=180, help="Timeout per loading phase")
    parser.add_argument("--poll", type=float, default=1, help="Log poll interval in seconds")
    parser.add_argument(
        "--vpk", type=Path, help="Optionally upload only its eboot.bin before launching"
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT
        / "work/vita-debug"
        / ("load-test-" + datetime.now().strftime("%Y%m%d-%H%M%S")),
    )
    args = parser.parse_args()
    if args.poll <= 0 or args.timeout <= 0:
        parser.error("--poll and --timeout must be positive")
    try:
        LoadTest(
            LoadTestConfig(
                ip=args.ip,
                command_port=args.command_port,
                ftp_port=args.ftp_port,
                timeout=args.timeout,
                poll=args.poll,
                vpk=args.vpk,
                output=args.output,
            )
        ).run()
    except (OSError, RuntimeError, ValueError, ftplib.Error, zipfile.BadZipFile) as error:
        print("Load test failed:", error, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

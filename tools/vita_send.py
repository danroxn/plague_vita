#!/usr/bin/env python3
"""Send the wrapper binary and start the game via VitaCompanion."""

from pathlib import Path
import argparse
import ftplib
import time
import zipfile

from vita_companion import TITLE_ID, VitaCompanion, read_wrapper

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", required=True)
    parser.add_argument("--ftp-port", type=int, default=1337)
    parser.add_argument("--command-port", type=int, default=1338)
    parser.add_argument("--vpk", type=Path, default=ROOT / "build/plague-vita.vpk")
    args = parser.parse_args()
    binary = read_wrapper(args.vpk)
    with VitaCompanion(
        args.ip, command_port=args.command_port, ftp_port=args.ftp_port, ftp_timeout=30
    ) as remote:
        remote.require_installed_app()
        print(remote.stop_all(), flush=True)
        # App teardown may still hold its executable after quit all returns.
        time.sleep(1)
        print("Uploading eboot.bin...", flush=True)
        remote.upload_binary(binary)
        time.sleep(1)
        print(remote.launch(TITLE_ID, require_launch_ack=True), flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, EOFError, RuntimeError, ValueError, ftplib.Error, zipfile.BadZipFile) as error:
        raise SystemExit(f"Send failed: {error}")
    except KeyboardInterrupt:
        raise SystemExit("Send interrupted.")

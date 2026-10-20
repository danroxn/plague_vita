"""VitaCompanion transport shared by deployment and hardware measurements."""

from __future__ import annotations

from io import BytesIO
from pathlib import Path
from types import TracebackType
import ftplib
import hashlib
import socket
import zipfile

TITLE_ID = "PLAGUEINC"
LOG_PATH = "ux0:/data/plague/port.log"
CONFIG_PATH = "ux0:/data/plague/port.cfg"


def read_wrapper(package: Path) -> bytes:
    """Read the local executable before beginning a deployment."""
    with zipfile.ZipFile(package) as archive:
        try:
            return archive.read("eboot.bin")
        except KeyError as error:
            raise ValueError("VPK does not contain eboot.bin") from error


class VitaCompanionError(RuntimeError):
    """Transport or acknowledgement failure, with no automatic command retry."""


class VitaCompanion:
    """Own the FTP session; each command uses a separate EOF-terminated socket."""

    def __init__(
        self,
        ip: str,
        *,
        command_port: int = 1338,
        ftp_port: int = 1337,
        ftp_timeout: float = 10,
        command_timeout: float = 10,
    ) -> None:
        self.ip = ip
        self.command_port = command_port
        self.ftp_port = ftp_port
        self.ftp_timeout = ftp_timeout
        self.command_timeout = command_timeout
        self._ftp: ftplib.FTP | None = None

    def __enter__(self) -> VitaCompanion:
        return self

    def __exit__(
        self,
        error_type: type[BaseException] | None,
        error: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        self.close()

    def command(self, text: str) -> str:
        try:
            with socket.create_connection(
                (self.ip, self.command_port), timeout=self.command_timeout
            ) as connection:
                connection.sendall((text + "\n").encode("ascii"))
                connection.shutdown(socket.SHUT_WR)
                response = bytearray()
                while block := connection.recv(8192):
                    response.extend(block)
        except (OSError, EOFError) as error:
            raise VitaCompanionError(
                f"Command transport {self.ip}:{self.command_port}: {error}"
            ) from error
        reply = response.decode("utf-8", errors="replace").strip()
        if not reply:
            raise VitaCompanionError("VitaCompanion returned no acknowledgement: " + text)
        return reply

    @staticmethod
    def _require_no_error(reply: str, operation: str) -> str:
        if "error" in reply.lower():
            raise VitaCompanionError(f"{operation} failed: {reply}")
        return reply

    def stop_all(self) -> str:
        reply = self.command("quit all")
        if reply.lower() != "apps quit.":
            raise VitaCompanionError(f"Stop failed: {reply}. Upload was not started.")
        return reply

    def stop_application(self, title_id: str = TITLE_ID) -> str:
        help_text = self.command("help")
        supported = {line.split()[0] for line in help_text.splitlines() if line.split()}
        verb = "quit" if "quit" in supported else "kill" if "kill" in supported else None
        if verb is None:
            raise VitaCompanionError("No supported per-application stop command")
        return self._require_no_error(self.command(f"{verb} {title_id}"), "Stop")

    def launch(self, title_id: str = TITLE_ID, *, require_launch_ack: bool = False) -> str:
        reply = self._require_no_error(self.command("launch " + title_id), "Launch")
        if require_launch_ack and "launch" not in reply.lower():
            raise VitaCompanionError("Launch was not acknowledged: " + reply)
        return reply

    def tap_front(self, x: int, y: int, *, hold_ms: int = 200) -> str:
        reply = self.command(
            f"press front-touch 0 {x} {y}; wait {hold_ms}ms; release front-touch 0"
        )
        normalized = reply.lower()
        if (
            "error" in normalized
            or normalized.count("input pressed") != 1
            or normalized.count("input released") != 1
        ):
            raise VitaCompanionError("Touch was not acknowledged: " + reply)
        return reply

    def release_front_touch(self) -> str:
        return self.command("release front-touch 0")

    def _connect(self) -> ftplib.FTP:
        if self._ftp is None:
            connection = ftplib.FTP()
            try:
                connection.connect(self.ip, self.ftp_port, timeout=self.ftp_timeout)
                connection.login()
            except BaseException:
                connection.close()
                raise
            self._ftp = connection
        return self._ftp

    def read_config(self) -> bytes | None:
        return self._read_file(CONFIG_PATH, missing_ok=True)

    def require_installed_app(self) -> None:
        path = "ux0:/app/" + TITLE_ID
        try:
            self._connect().cwd(path)
        except ftplib.error_perm as error:
            if str(error).startswith("550"):
                raise VitaCompanionError(
                    f"Application {TITLE_ID} is not accessible at {path}. "
                    "Install build/plague-vita.vpk with VitaShell first, also after a title ID "
                    "change; make send only updates an existing installation."
                ) from error
            raise

    def read_log(self, missing_ok: bool = False) -> bytes:
        return self._read_file(LOG_PATH, missing_ok=missing_ok) or b""

    def _read_file(self, path: str, *, missing_ok: bool) -> bytes | None:
        output = BytesIO()
        try:
            self._connect().retrbinary("RETR " + path, output.write)
        except ftplib.error_perm as error:
            if missing_ok and str(error).startswith("550"):
                return None
            raise
        return output.getvalue()

    def remove_log(self) -> None:
        self._connect().delete(LOG_PATH)

    def upload_binary(self, binary: bytes, verify: bool = False) -> str:
        target = "ux0:/app/" + TITLE_ID + "/eboot.bin"
        connection = self._connect()
        connection.storbinary("STOR " + target, BytesIO(binary))
        expected = hashlib.sha256(binary).hexdigest()
        if verify:
            actual = hashlib.sha256()
            connection.retrbinary("RETR " + target, actual.update)
            if actual.hexdigest() != expected:
                raise VitaCompanionError("Uploaded wrapper failed verification")
        return expected

    def close(self) -> None:
        if self._ftp is not None:
            self._ftp.close()
            self._ftp = None

"""Maintenance operations for the bundled or development yt-dlp runtime."""

import json
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path


class YtDlpMaintenanceService:
    """Update and inspect yt-dlp without coupling that work to app workflows."""

    PACKAGE_METADATA_URL = "https://pypi.org/pypi/yt-dlp/json"
    USER_AGENT = "THM-VocalPanel/1.0"

    def update(self) -> dict:
        if getattr(sys, "frozen", False):
            return self._update_sideload()

        command = [sys.executable, "-m", "pip", "install", "-U", "yt-dlp"]
        process = subprocess.run(command, capture_output=True, text=True, timeout=180)
        stdout, stderr = process.stdout or "", process.stderr or ""
        if process.returncode != 0:
            raise RuntimeError(
                "Cập nhật yt-dlp thất bại.\n\n"
                f"STDOUT:\n{stdout[-1600:]}\n\nSTDERR:\n{stderr[-1600:]}"
            )
        return {"ok": True, "command": " ".join(command), "stdout": stdout, "stderr": stderr}

    def _package_metadata(self, timeout: int) -> dict:
        request = urllib.request.Request(self.PACKAGE_METADATA_URL, headers={"User-Agent": self.USER_AGENT})
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode())

    def _update_sideload(self) -> dict:
        """Download a wheel and atomically replace yt_dlp beside a frozen app."""
        metadata = self._package_metadata(timeout=30)
        version = metadata["info"]["version"]
        wheel_url = next(
            (item["url"] for item in metadata["urls"] if item["filename"].endswith("-py3-none-any.whl")),
            None,
        )
        if not wheel_url:
            raise RuntimeError(f"Khong tim thay wheel yt-dlp {version} tren PyPI")

        sideload_dir = Path(sys.executable).parent / "_sideload"
        try:
            sideload_dir.mkdir(parents=True, exist_ok=True)
            write_test = sideload_dir / "_write_test"
            write_test.write_text("ok")
            write_test.unlink()
        except PermissionError as error:
            raise RuntimeError(
                f"Khong co quyen ghi vao:\n{sideload_dir}\n\nHay thu chay app voi quyen Administrator."
            ) from error

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            wheel_path = temp_path / "yt_dlp.whl"
            urllib.request.urlretrieve(wheel_url, wheel_path)
            with zipfile.ZipFile(wheel_path, "r") as archive:
                for member in archive.namelist():
                    if member.startswith("yt_dlp/"):
                        archive.extract(member, temp_dir)

            source, target, previous = (
                temp_path / "yt_dlp",
                sideload_dir / "yt_dlp",
                sideload_dir / "_yt_dlp_old",
            )
            if previous.exists():
                shutil.rmtree(previous, ignore_errors=True)
            if target.exists():
                shutil.move(str(target), str(previous))
            shutil.move(str(source), str(target))
            if previous.exists():
                shutil.rmtree(previous, ignore_errors=True)
        return {"ok": True, "version": version}

    def version_info(self) -> dict:
        """Return current/latest versions; network failure is a reportable result."""
        result = {"current": "", "latest": "", "is_outdated": False, "error": None}
        try:
            import yt_dlp

            result["current"] = yt_dlp.version.__version__
        except Exception as error:
            result["error"] = f"Không đọc version hiện tại: {error}"
            return result

        try:
            result["latest"] = self._package_metadata(timeout=8)["info"]["version"]
            result["is_outdated"] = self._version_tuple(result["current"]) < self._version_tuple(result["latest"])
        except Exception as error:
            result["error"] = f"Không kiểm tra được version mới: {error}"
        return result

    @staticmethod
    def _version_tuple(version: str) -> tuple[int, ...]:
        try:
            return tuple(int(part) for part in version.split("."))
        except ValueError:
            return (0,)

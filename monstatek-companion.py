#!/usr/bin/env python3
"""Single-file local companion for MonstaTek M1 flashing.

This exposes a tiny localhost HTTP API so the website can delegate M1 DFU
flashing to STM32CubeProgrammer instead of WebUSB.
"""

from __future__ import annotations

import base64
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


COMPANION_VERSION = "0.2.1"
DEFAULT_HOST = os.environ.get("MONSTATEK_COMPANION_HOST", "127.0.0.1")
DEFAULT_PORT = int(os.environ.get("MONSTATEK_COMPANION_PORT", "32413"))
MAX_JSON_BODY_BYTES = 15 * 1024 * 1024
JOB_RETENTION_LIMIT = 20
JOBS: dict[str, dict[str, Any]] = {}
JOBS_LOCK = threading.Lock()


def default_cli_candidates() -> list[str]:
    candidates = [
        os.environ.get("STM32CUBEPROG_CLI"),
        "/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI",
        "/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/MacOs/api/lib/STM32_Programmer_CLI",
        "/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
        "/opt/st/stm32cubeprogrammer/bin/STM32_Programmer_CLI",
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    ]
    return [candidate for candidate in candidates if candidate]


def detect_cubeprogrammer_cli() -> str | None:
    for candidate in default_cli_candidates():
        path = Path(candidate)
        if path.is_file():
            return str(path)
    return None


def parse_usb_ports(output: str) -> list[str]:
    matches = re.findall(r"\busb\d+\b", output, flags=re.IGNORECASE)
    seen: list[str] = []
    for match in matches:
        normalized = match.lower()
        if normalized not in seen:
            seen.append(normalized)
    return seen


def run_command(command: str, args: list[str], timeout_seconds: int = 120) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [command, *args],
        capture_output=True,
        text=True,
        timeout=timeout_seconds,
        check=False,
    )


def list_dfu_devices(cli_path: str) -> dict[str, Any]:
    result = run_command(cli_path, ["-l", "usb"], timeout_seconds=15)
    raw_output = f"{result.stdout}\n{result.stderr}".strip()
    return {
        "ports": parse_usb_ports(raw_output),
        "rawOutput": raw_output,
        "code": result.returncode,
    }


def sanitize_filename(filename: str) -> str:
    return re.sub(r"[^a-zA-Z0-9._-]", "_", filename or "monstatek-firmware.bin")


def trim_jobs() -> None:
    with JOBS_LOCK:
        if len(JOBS) <= JOB_RETENTION_LIMIT:
            return

        completed = [
            (job_id, data.get("updatedAt", 0))
            for job_id, data in JOBS.items()
            if data.get("status") in {"completed", "failed"}
        ]
        completed.sort(key=lambda item: item[1])

        while len(JOBS) > JOB_RETENTION_LIMIT and completed:
            job_id, _ = completed.pop(0)
            JOBS.pop(job_id, None)


def update_job(job_id: str, **fields: Any) -> dict[str, Any]:
    with JOBS_LOCK:
        current = JOBS.get(job_id)
        if current is None:
            raise KeyError(f"Unknown job: {job_id}")

        current.update(fields)
        current["updatedAt"] = current.get("updatedAt", 0) + 1
        snapshot = dict(current)

    trim_jobs()
    return snapshot


def get_job(job_id: str) -> dict[str, Any] | None:
    with JOBS_LOCK:
        current = JOBS.get(job_id)
        return dict(current) if current else None


def create_job(filename: str, address: str, go_address: str) -> dict[str, Any]:
    job_id = uuid.uuid4().hex
    job = {
        "ok": True,
        "jobId": job_id,
        "status": "queued",
        "filename": filename,
        "address": address,
        "goAddress": go_address,
        "progress": 0,
        "stage": "Queued",
        "message": "Queued in local companion.",
        "port": None,
        "cliPath": None,
        "stdout": "",
        "stderr": "",
        "rawOutput": "",
        "error": None,
        "updatedAt": 0,
    }
    with JOBS_LOCK:
        JOBS[job_id] = job
    return dict(job)


def parse_cubeprogrammer_record(record: str, current_progress: int, current_stage: str) -> tuple[int, str] | None:
    line = record.strip()
    if not line:
        return None

    lowered = line.lower()
    percent_match = re.search(r"(\d{1,3})%", line)
    if percent_match:
        percent = max(0, min(100, int(percent_match.group(1))))
        return percent, "Writing firmware"

    stage_rules = [
        ("opening and parsing file", 8, "Preparing firmware"),
        ("memory programming", 12, "Programming flash"),
        ("download verified successfully", 96, "Verifying flash"),
        ("file download complete", 97, "Finalizing flash"),
        ("reset system", 99, "Resetting M1"),
        ("resetting system", 99, "Resetting M1"),
        ("start operation achieved successfully", 99, "Waiting for reboot"),
        ("starting embedded software successfully", 99, "Waiting for reboot"),
    ]
    for needle, progress, stage in stage_rules:
        if needle in lowered:
            return max(current_progress, progress), stage

    return None


def run_flash_job(job_id: str, cli_path: str, filename: str, address: str, go_address: str, requested_port: str, firmware_bytes: bytes) -> None:
    temp_path: str | None = None
    try:
        update_job(job_id, status="running", progress=2, stage="Preparing STM32CubeProgrammer", cliPath=cli_path)

        with tempfile.NamedTemporaryFile(prefix="monstatek-", suffix=f"-{filename}", delete=False) as temp_file:
            temp_file.write(firmware_bytes)
            temp_path = temp_file.name

        listed = list_dfu_devices(cli_path)
        selected_port = requested_port or (listed["ports"][0] if listed["ports"] else "usb1")
        args = [
            "-c",
            f"port={selected_port}",
            "-d",
            temp_path,
            address,
            "-v",
            "-rst",
        ]

        update_job(
            job_id,
            port=selected_port,
            progress=5,
            stage="Launching STM32CubeProgrammer",
            message=f"STM32CubeProgrammer started for {filename} via {selected_port}.",
        )

        process = subprocess.Popen(
            [cli_path, *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        combined_output = ""
        buffer = ""
        current_progress = 5
        current_stage = "Launching STM32CubeProgrammer"
        assert process.stdout is not None

        while True:
            chunk = process.stdout.read(1)
            if chunk == "" and process.poll() is not None:
                break
            if chunk == "":
                continue

            combined_output += chunk
            if chunk in "\r\n":
                record = buffer.strip()
                buffer = ""
                parsed = parse_cubeprogrammer_record(record, current_progress, current_stage)
                if parsed:
                    current_progress, current_stage = parsed
                    update_job(job_id, progress=current_progress, stage=current_stage)
            else:
                buffer += chunk

        if buffer.strip():
            parsed = parse_cubeprogrammer_record(buffer.strip(), current_progress, current_stage)
            if parsed:
                current_progress, current_stage = parsed
                update_job(job_id, progress=current_progress, stage=current_stage)

        return_code = process.wait()
        raw_output = combined_output.strip()

        if return_code != 0:
            update_job(
                job_id,
                ok=False,
                status="failed",
                stage="STM32CubeProgrammer failed",
                error=f"STM32CubeProgrammer CLI exited with code {return_code}.",
                rawOutput=raw_output,
                stdout=combined_output,
                stderr="",
            )
            return

        update_job(
            job_id,
            status="completed",
            progress=100,
            stage="Complete",
            message=f"STM32CubeProgrammer flashed {filename} via {selected_port} and reset the M1.",
            rawOutput=raw_output,
            stdout=combined_output,
            stderr="",
        )
    except Exception as error:
        update_job(
            job_id,
            ok=False,
            status="failed",
            stage="Companion error",
            error=str(error),
        )
    finally:
        if temp_path:
            try:
                os.unlink(temp_path)
            except OSError:
                pass


class CompanionHandler(BaseHTTPRequestHandler):
    server_version = f"MonstaTekCompanion/{COMPANION_VERSION}"

    def end_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def send_json(self, status_code: int, payload: dict[str, Any]) -> None:
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def read_json_body(self) -> dict[str, Any]:
        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length > MAX_JSON_BODY_BYTES:
            raise ValueError(f"Request body exceeded {MAX_JSON_BODY_BYTES} bytes.")

        raw_body = self.rfile.read(content_length)
        try:
            return json.loads(raw_body.decode("utf-8") or "{}")
        except Exception as error:
            raise ValueError(f"Invalid JSON body: {error}") from error

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        try:
            if self.path == "/health":
                self.handle_health()
                return

            job_match = re.fullmatch(r"/monstatek/jobs/([a-f0-9]+)", self.path)
            if job_match:
                self.handle_job_status(job_match.group(1))
                return

            self.send_json(404, {"ok": False, "error": f"No route for GET {self.path}."})
        except Exception as error:
            self.send_json(500, {"ok": False, "error": str(error)})

    def do_POST(self) -> None:  # noqa: N802
        try:
            if self.path == "/monstatek/flash":
                self.handle_flash()
                return

            self.send_json(404, {"ok": False, "error": f"No route for POST {self.path}."})
        except Exception as error:
            self.send_json(500, {"ok": False, "error": str(error)})

    def handle_health(self) -> None:
        cli_path = detect_cubeprogrammer_cli()
        if not cli_path:
            self.send_json(
                200,
                {
                    "ok": True,
                    "version": COMPANION_VERSION,
                    "cubeProgrammer": {"found": False, "cliPath": None},
                    "dfuDevices": {"count": 0, "ports": [], "rawOutput": ""},
                },
            )
            return

        try:
            listed = list_dfu_devices(cli_path)
            dfu_devices = {
                "count": len(listed["ports"]),
                "ports": listed["ports"],
                "rawOutput": listed["rawOutput"],
            }
        except Exception as error:
            dfu_devices = {
                "count": 0,
                "ports": [],
                "rawOutput": str(error),
            }

        self.send_json(
            200,
            {
                "ok": True,
                "version": COMPANION_VERSION,
                "cubeProgrammer": {"found": True, "cliPath": cli_path},
                "dfuDevices": dfu_devices,
            },
        )

    def handle_job_status(self, job_id: str) -> None:
        job = get_job(job_id)
        if not job:
            self.send_json(404, {"ok": False, "error": f"No flash job found for {job_id}."})
            return
        self.send_json(200, job)

    def handle_flash(self) -> None:
        cli_path = detect_cubeprogrammer_cli()
        if not cli_path:
            self.send_json(503, {"ok": False, "error": "STM32CubeProgrammer CLI was not found on this machine."})
            return

        body = self.read_json_body()
        firmware_base64 = body.get("firmwareBase64", "")
        if not isinstance(firmware_base64, str) or not firmware_base64:
            self.send_json(400, {"ok": False, "error": "Missing firmwareBase64 in request body."})
            return

        filename = sanitize_filename(str(body.get("filename", "monstatek-firmware.bin")))
        address = str(body.get("address", "0x08000000"))
        go_address = str(body.get("goAddress", "0x08000000"))
        requested_port = str(body.get("port", "")).lower()
        firmware_bytes = base64.b64decode(firmware_base64)
        job = create_job(filename, address, go_address)
        thread = threading.Thread(
            target=run_flash_job,
            args=(job["jobId"], cli_path, filename, address, go_address, requested_port, firmware_bytes),
            daemon=True,
        )
        thread.start()
        self.send_json(202, job)

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stdout.write(f"{self.address_string()} - {fmt % args}\n")


def main() -> None:
    server = ThreadingHTTPServer((DEFAULT_HOST, DEFAULT_PORT), CompanionHandler)
    print(f"MonstaTek Python companion listening on http://{DEFAULT_HOST}:{DEFAULT_PORT}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

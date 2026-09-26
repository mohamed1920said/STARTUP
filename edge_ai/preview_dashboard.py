#!/usr/bin/env python3
"""Serve the dashboard with deterministic API fixtures for visual QA only."""

from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
import socket
import struct
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

AI_STATUS = {
    "model_name": "AMR Tunisia Hybrid Edge AI v1",
    "model_version": 20260807,
    "shadow_mode": True,
    "weather": {
        "valid": True,
        "temperature_c_24h": 27.4,
        "rain_mm_24h": 5.8,
        "rain_probability": 0.72,
        "et0_mm_24h": 3.9,
    },
    "zones": [{
        "model_version": 20260807,
        "shadow_mode": True,
        "sensor_id": 1,
        "actuator_id": 2,
        "forecast_valid": True,
        "sensor_age_s": 10,
        "fault_checks_limited": True,
        "valid": True,
        "irrigation_needed": True,
        "irrigation_now": False,
        "probability": 0.84,
        "water_mm": 12.4,
        "runtime_min": 30,
        "total_runtime_min": 74,
        "cycles": 3,
        "wait_hours": 24,
        "wait_for_rain": True,
        "slow_drying": False,
        "drying_rate": -2.1,
        "forecast_temp_c": 27.4,
        "forecast_rain_mm": 5.8,
        "rain_probability": 0.72,
        "forecast_et0_mm": 3.9,
        "watering_effect": "moisture_increased",
        "moisture_increase_pct": 4.6,
        "fault": "normal",
        "fault_confidence": 0.99,
        "reason": "Wait for likely rain: 72% probability, 5.8 mm predicted",
    }],
}

WEATHER = {"type": "weather", "temp": 28.4, "hum": 52.0, "pres": 1012.6,
           "rain": 0.0, "wind": 2.1, "dir": 90, "lux": 18200, "bat": 3950, "age_ms": 12000}
SENSOR = {"type": "sensor", "id": 1, "moisture": 31.8, "moisture_raw": 1832,
          "temp": 24.6, "batt": 3.87, "rssi": -64, "seq": 158, "errors": 0, "age_ms": 10000}
ACTUATOR = {"type": "actuator", "id": 2, "valve": False, "commanded": False,
            "feedback_valid": False, "batt": 3.92, "flow_lpm": None,
            "pressure_bar": None, "tank_pct": None, "errors": 9, "age_ms": 5000}
NODES = [{"id": 1, "type": 1, "alias": "Olive grove", "lastSeq": 158},
         {"id": 2, "type": 2, "alias": "Main valve", "lastSeq": 182,
          "autoMode": False, "threshold": 45, "sensorId": 1}]


class PreviewHandler(SimpleHTTPRequestHandler):
    scenario = "healthy"

    def send_json(self, payload: object) -> None:
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
        path = self.path.split("?", 1)[0]
        if path == "/ws":
            self.serve_websocket()
            return
        status = copy.deepcopy(AI_STATUS)
        if self.scenario == "waiting":
            status["zones"] = []
            status["weather"] = {"valid": False}
            status["reason"] = "No soil reading received. Check sensor power, range, node ID and key."
        fixtures = {
            "/api/ai/status": status,
            "/api/health": {"uptime": 86400, "free_heap": 180224, "rssi": -51,
                            "boot_id": "preview", "reset_reason": "Power on", "min_free_heap": 163840,
                            "lora_ready": True, "preview": True},
            "/api/nodes": NODES,
            "/api/telemetry": {"weather": WEATHER, "sensors": [SENSOR] if self.scenario == "healthy" else [], "actuators": [ACTUATOR]},
            "/api/cloud-config": {"url": "", "apiKeySet": False, "caCertSet": False},
            "/api/dataset/status": {"records": 6576, "dropped": 0, "bytes": 420000, "archive_bytes": 0},
            "/api/field-config": [],
        }
        if path in fixtures:
            self.send_json(fixtures[path])
            return
        super().do_GET()

    def serve_websocket(self) -> None:
        """Read-only fixture stream. Commands are never forwarded to hardware."""
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.send_error(400)
            return
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.connection.settimeout(1)
        next_send = 0
        try:
            while True:
                if time.monotonic() >= next_send:
                    payload = json.dumps({"type": "log", "level": "preview", "msg": "Demo data — no hardware connected"}).encode()
                    prefix = bytes([0x81, len(payload)]) if len(payload) < 126 else bytes([0x81, 126]) + struct.pack("!H", len(payload))
                    self.wfile.write(prefix + payload)
                    self.wfile.flush()
                    next_send = time.monotonic() + 10
                try:
                    data = self.connection.recv(4096)
                    if not data or data[0] & 0x0F == 8:
                        break
                except socket.timeout:
                    pass
        except (ConnectionError, OSError):
            pass
        self.close_connection = True

    def log_message(self, format: str, *args: object) -> None:
        return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--scenario", choices=("healthy", "waiting"), default="healthy")
    args = parser.parse_args()
    PreviewHandler.scenario = args.scenario
    handler = lambda *handler_args, **kwargs: PreviewHandler(  # noqa: E731
        *handler_args, directory=str(ROOT / "data"), **kwargs
    )
    ThreadingHTTPServer(("127.0.0.1", args.port), handler).serve_forever()


if __name__ == "__main__":
    main()

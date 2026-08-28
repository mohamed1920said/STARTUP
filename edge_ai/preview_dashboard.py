#!/usr/bin/env python3
"""Serve the dashboard with deterministic API fixtures for visual QA only."""

from __future__ import annotations

import argparse
import json
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
        "actuator_id": 257,
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


class PreviewHandler(SimpleHTTPRequestHandler):
    def send_json(self, payload: object) -> None:
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:  # noqa: N802 - stdlib handler API
        fixtures = {
            "/api/ai/status": AI_STATUS,
            "/api/health": {"uptime": 86400, "free_heap": 240000, "rssi": -51},
            "/api/nodes": [],
            "/api/cloud-config": {"url": "", "apiKeySet": False, "caCertSet": False},
            "/api/dataset/status": {"records": 6576, "dropped": 0, "bytes": 420000, "archive_bytes": 0},
            "/api/field-config": [],
        }
        path = self.path.split("?", 1)[0]
        if path in fixtures:
            self.send_json(fixtures[path])
            return
        super().do_GET()

    def log_message(self, format: str, *args: object) -> None:
        return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    handler = lambda *handler_args, **kwargs: PreviewHandler(  # noqa: E731
        *handler_args, directory=str(ROOT / "data"), **kwargs
    )
    ThreadingHTTPServer(("127.0.0.1", args.port), handler).serve_forever()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Fast regression checks for the generated Edge-AI model bank."""

from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path

import numpy as np

from edge_ai import train_edge_ai as trainer


ROOT = Path(__file__).resolve().parents[1]
DATASET = trainer.DEFAULT_DATASET
MANIFEST = trainer.DEFAULT_MANIFEST
HEADER = trainer.DEFAULT_HEADER


def linear(model: dict, values: np.ndarray) -> np.ndarray:
    mean = np.asarray(model["mean"], dtype=float)
    scale = np.asarray(model["scale"], dtype=float)
    weights = np.asarray(model["weights"], dtype=float)
    return float(model["bias"]) + ((values - mean) / scale) @ weights


class EdgeAIModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        cls.frame = trainer.prepare_frame(DATASET)

    def test_dataset_hash_and_generated_header_match(self) -> None:
        digest = hashlib.sha256(DATASET.read_bytes()).hexdigest()
        self.assertEqual(digest, self.manifest["dataset_sha256"])
        self.assertIn(digest, HEADER.read_text(encoding="utf-8"))

    def test_all_model_dimensions_match_firmware_features(self) -> None:
        models = self.manifest["models"]
        self.assertEqual(len(models["irrigation"]["weights"]), len(trainer.IRRIGATION_FEATURES))
        self.assertEqual(len(models["water"]["weights"]), len(trainer.IRRIGATION_FEATURES))
        self.assertEqual(len(models["weather"]["temperature"]["weights"]), len(trainer.WEATHER_FEATURES))
        self.assertEqual(np.asarray(models["fault"]["weights"]).shape,
                         (len(trainer.FAULT_CLASSES), len(trainer.FAULT_FEATURES)))

    def test_dry_field_scores_above_wet_field(self) -> None:
        row = self.frame.iloc[len(self.frame) // 2].copy()
        dry = trainer.matrix(row.to_frame().T, trainer.IRRIGATION_FEATURES)[0]
        wet = dry.copy()
        moisture_index = trainer.IRRIGATION_FEATURES.index("moisture_pct_sim")
        delta_index = trainer.IRRIGATION_FEATURES.index("moisture_delta_pct")
        dry[moisture_index], dry[delta_index] = 20.0, 25.0
        wet[moisture_index], wet[delta_index] = 85.0, -40.0
        model = self.manifest["models"]["irrigation"]
        dry_probability = 1.0 / (1.0 + np.exp(-linear(model, dry)))
        wet_probability = 1.0 / (1.0 + np.exp(-linear(model, wet)))
        self.assertGreater(dry_probability, wet_probability)
        self.assertGreater(dry_probability, 0.5)
        self.assertLess(wet_probability, 0.5)

    def test_forecasts_are_finite_over_holdout(self) -> None:
        holdout = self.frame[self.frame["date"].dt.year == 2025].iloc[::31]
        values = trainer.matrix(holdout, trainer.WEATHER_FEATURES)
        for model in self.manifest["models"]["weather"].values():
            prediction = linear(model, values)
            self.assertTrue(np.all(np.isfinite(prediction)))

    def test_fault_model_recognizes_injected_fault_rows(self) -> None:
        fault_rows = self.frame[self.frame["anomaly_label"] != "normal"]
        values = trainer.matrix(fault_rows, trainer.FAULT_FEATURES)
        model = self.manifest["models"]["fault"]
        prediction, _ = trainer.fault_predict(
            {key: np.asarray(value) if isinstance(value, list) else value for key, value in model.items()}, values
        )
        truth = fault_rows["anomaly_label"].map({name: i for i, name in enumerate(trainer.FAULT_CLASSES)}).to_numpy()
        self.assertGreaterEqual(float(np.mean(prediction == truth)), 0.90)


if __name__ == "__main__":
    unittest.main(verbosity=2)

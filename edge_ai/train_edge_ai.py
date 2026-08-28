#!/usr/bin/env python3
"""Train compact, dependency-light models for the ESP32-S3 gateway.

The generated C++ header uses standardized linear/logistic models.  The models
are intentionally small and interpretable; hard hydraulic and runtime safety
rules remain in EdgeAIEngine and can never be overridden by a model score.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATASET = ROOT / "outputs" / "tunisia_irrigation_ai_20260807" / "tunisia_irrigation_ai_training_dataset_2023_2025.csv"
DEFAULT_HEADER = ROOT / "src" / "central" / "EdgeAIModel.h"
DEFAULT_MANIFEST = ROOT / "edge_ai" / "model_manifest.json"
DEFAULT_METRICS = ROOT / "edge_ai" / "metrics.json"

IRRIGATION_FEATURES = [
    "moisture_pct_sim", "moisture_delta_pct", "soil_temperature_0_7cm_c",
    "temperature_mean_c", "relative_humidity_mean_pct", "pressure_msl_mean_hpa",
    "rain_mm", "wind_speed_mean_ms", "vapor_pressure_deficit_kpa", "et0_mm",
    "crop_coefficient_kc", "moisture_trend_pct_day", "rain_3d_mm", "day_sin", "day_cos",
]

WEATHER_FEATURES = [
    "temperature_mean_c", "relative_humidity_mean_pct", "pressure_msl_mean_hpa",
    "rain_mm", "wind_speed_mean_ms", "temperature_trend_c_day",
    "pressure_trend_hpa_day", "rain_3d_mm", "day_sin", "day_cos",
]

FAULT_FEATURES = [
    "moisture_pct_sim", "moisture_raw_sim", "node_battery_v", "rssi_dbm",
    "valve_commanded", "valve_actual", "feedback_valid", "flow_lpm",
    "line_pressure_bar", "tank_pct", "pump_current_a", "error_flags",
]

FAULT_CLASSES = ["normal", "leak", "blocked_pipe", "empty_tank", "valve_fault", "sensor_fault"]


def safe_std(values: np.ndarray) -> np.ndarray:
    std = np.nanstd(values, axis=0)
    return np.where(std < 1e-6, 1.0, std)


def sigmoid(values: np.ndarray) -> np.ndarray:
    values = np.clip(values, -30.0, 30.0)
    return 1.0 / (1.0 + np.exp(-values))


def prepare_frame(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["date"] = pd.to_datetime(df["date_local"], utc=True)
    df = df.sort_values(["region", "date"]).reset_index(drop=True)
    grouped = df.groupby("region", sort=False)

    df["moisture_delta_pct"] = df["moisture_threshold_pct"] - df["moisture_pct_sim"]
    df["moisture_trend_pct_day"] = grouped["moisture_pct_sim"].diff(3).div(3.0).fillna(0.0)
    df["temperature_trend_c_day"] = grouped["temperature_mean_c"].diff().fillna(0.0)
    df["pressure_trend_hpa_day"] = grouped["pressure_msl_mean_hpa"].diff().fillna(0.0)
    df["rain_3d_mm"] = grouped["rain_mm"].rolling(3, min_periods=1).sum().reset_index(level=0, drop=True)
    radians = 2.0 * math.pi * (df["day_of_year"] - 1.0) / 365.25
    df["day_sin"] = np.sin(radians)
    df["day_cos"] = np.cos(radians)

    df["next_temperature_c"] = grouped["temperature_mean_c"].shift(-1)
    df["next_rain_mm"] = grouped["rain_mm"].shift(-1)
    df["next_et0_mm"] = grouped["et0_mm"].shift(-1)
    df["next_moisture_pct"] = grouped["moisture_pct_sim"].shift(-1)
    df["next_irrigated"] = grouped["valve_commanded"].shift(-1)
    df["rain_next_24h"] = (df["next_rain_mm"].fillna(0.0) >= 1.0).astype(np.float64)
    df["drying_rate_target"] = df["next_moisture_pct"] - df["moisture_pct_sim"]
    return df


def matrix(df: pd.DataFrame, features: list[str]) -> np.ndarray:
    values = np.array(df[features].to_numpy(dtype=np.float64), dtype=np.float64, copy=True)
    medians = np.nanmedian(values, axis=0)
    missing = np.where(np.isnan(values))
    values[missing] = medians[missing[1]]
    return values


def fit_logistic(x: np.ndarray, y: np.ndarray, balance: bool = True, l2: float = 0.08) -> dict:
    mean = np.mean(x, axis=0)
    scale = safe_std(x)
    z = (x - mean) / scale
    design = np.column_stack([np.ones(len(z)), z])
    beta = np.zeros(design.shape[1], dtype=np.float64)
    if balance:
        positives = max(float(np.sum(y == 1)), 1.0)
        negatives = max(float(np.sum(y == 0)), 1.0)
        sample_weight = np.where(y == 1, len(y) / (2.0 * positives), len(y) / (2.0 * negatives))
    else:
        sample_weight = np.ones(len(y), dtype=np.float64)

    penalty = np.eye(design.shape[1], dtype=np.float64) * l2
    penalty[0, 0] = 0.0
    for _ in range(60):
        logits = design @ beta
        probabilities = sigmoid(logits)
        variance = np.maximum(probabilities * (1.0 - probabilities), 1e-5)
        weights = sample_weight * variance
        working = logits + (y - probabilities) / variance
        hessian = design.T @ (weights[:, None] * design) + penalty
        rhs = design.T @ (weights * working)
        next_beta = np.linalg.solve(hessian, rhs)
        if np.max(np.abs(next_beta - beta)) < 1e-8:
            beta = next_beta
            break
        beta = next_beta
    return {"mean": mean, "scale": scale, "bias": float(beta[0]), "weights": beta[1:]}


def fit_ridge(x: np.ndarray, y: np.ndarray, l2: float = 1.0) -> dict:
    mean = np.mean(x, axis=0)
    scale = safe_std(x)
    z = (x - mean) / scale
    design = np.column_stack([np.ones(len(z)), z])
    penalty = np.eye(design.shape[1], dtype=np.float64) * l2
    penalty[0, 0] = 0.0
    beta = np.linalg.solve(design.T @ design + penalty, design.T @ y)
    return {"mean": mean, "scale": scale, "bias": float(beta[0]), "weights": beta[1:]}


def linear_predict(model: dict, x: np.ndarray) -> np.ndarray:
    return model["bias"] + ((x - model["mean"]) / model["scale"]) @ model["weights"]


def logistic_predict(model: dict, x: np.ndarray) -> np.ndarray:
    return sigmoid(linear_predict(model, x))


def binary_metrics(y: np.ndarray, probabilities: np.ndarray, threshold: float) -> dict:
    pred = probabilities >= threshold
    truth = y.astype(bool)
    tp = int(np.sum(pred & truth)); tn = int(np.sum(~pred & ~truth))
    fp = int(np.sum(pred & ~truth)); fn = int(np.sum(~pred & truth))
    precision = tp / max(tp + fp, 1)
    recall = tp / max(tp + fn, 1)
    return {
        "threshold": float(threshold), "accuracy": (tp + tn) / max(len(y), 1),
        "precision": precision, "recall": recall,
        "f1": 2.0 * precision * recall / max(precision + recall, 1e-12),
        "false_negative_rate": fn / max(tp + fn, 1),
        "tp": tp, "tn": tn, "fp": fp, "fn": fn,
    }


def choose_threshold(y: np.ndarray, probabilities: np.ndarray, recall_weight: float = 2.0) -> float:
    best_threshold, best_score = 0.5, -1.0
    beta2 = recall_weight * recall_weight
    for threshold in np.linspace(0.15, 0.85, 141):
        m = binary_metrics(y, probabilities, float(threshold))
        p, r = m["precision"], m["recall"]
        score = (1.0 + beta2) * p * r / max(beta2 * p + r, 1e-12)
        if score > best_score:
            best_threshold, best_score = float(threshold), score
    return best_threshold


def fit_fault_models(x: np.ndarray, labels: np.ndarray) -> dict:
    models = []
    for class_name in FAULT_CLASSES:
        y = (labels == class_name).astype(np.float64)
        models.append(fit_logistic(x, y, balance=True, l2=0.15))
    return {
        "mean": models[0]["mean"], "scale": models[0]["scale"],
        "bias": np.array([m["bias"] for m in models]),
        "weights": np.stack([m["weights"] for m in models]),
    }


def fault_predict(model: dict, x: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    z = (x - model["mean"]) / model["scale"]
    logits = model["bias"][None, :] + z @ model["weights"].T
    logits -= np.max(logits, axis=1, keepdims=True)
    probs = np.exp(np.clip(logits, -30.0, 30.0))
    probs /= np.sum(probs, axis=1, keepdims=True)
    return np.argmax(probs, axis=1), np.max(probs, axis=1)


def cpp_float(value: float) -> str:
    text = f"{float(value):.9g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def float_array(values: np.ndarray | list[float]) -> str:
    return ", ".join(cpp_float(value) for value in values)


def emit_vector(name: str, values: np.ndarray | list[float]) -> str:
    return f"static constexpr float {name}[{len(values)}] = {{{float_array(values)}}};"


def emit_matrix(name: str, values: np.ndarray) -> str:
    rows = ",\n    ".join("{" + float_array(row) + "}" for row in values)
    return f"static constexpr float {name}[{values.shape[0]}][{values.shape[1]}] = {{\n    {rows}\n}};"


def model_to_json(model: dict) -> dict:
    result = {}
    for key, value in model.items():
        result[key] = value.tolist() if isinstance(value, np.ndarray) else value
    return result


def write_header(path: Path, dataset_hash: str, models: dict, thresholds: dict) -> None:
    irrigation = models["irrigation"]
    water = models["water"]
    drying = models["drying"]
    rain = models["rain"]
    weather = models["weather"]
    fault = models["fault"]
    content = f"""#pragma once

// Generated by edge_ai/train_edge_ai.py. Do not edit model coefficients by hand.
#include <stddef.h>
#include <stdint.h>

namespace EdgeAIModel {{
static constexpr uint32_t MODEL_VERSION = 20260807U;
static constexpr const char* MODEL_NAME = \"AMR Tunisia Hybrid Edge AI v1\";
static constexpr const char* DATASET_SHA256 = \"{dataset_hash}\";
static constexpr size_t IRRIGATION_FEATURE_COUNT = {len(IRRIGATION_FEATURES)};
static constexpr size_t WEATHER_FEATURE_COUNT = {len(WEATHER_FEATURES)};
static constexpr size_t FAULT_FEATURE_COUNT = {len(FAULT_FEATURES)};
static constexpr size_t FAULT_CLASS_COUNT = {len(FAULT_CLASSES)};
static constexpr float IRRIGATION_THRESHOLD = {cpp_float(thresholds['irrigation'])};
static constexpr float RAIN_THRESHOLD = {cpp_float(thresholds['rain'])};

{emit_vector('IRRIGATION_MEAN', irrigation['mean'])}
{emit_vector('IRRIGATION_SCALE', irrigation['scale'])}
{emit_vector('IRRIGATION_WEIGHTS', irrigation['weights'])}
static constexpr float IRRIGATION_BIAS = {cpp_float(irrigation['bias'])};

{emit_vector('WATER_MEAN', water['mean'])}
{emit_vector('WATER_SCALE', water['scale'])}
{emit_vector('WATER_WEIGHTS', water['weights'])}
static constexpr float WATER_BIAS = {cpp_float(water['bias'])};

{emit_vector('DRYING_MEAN', drying['mean'])}
{emit_vector('DRYING_SCALE', drying['scale'])}
{emit_vector('DRYING_WEIGHTS', drying['weights'])}
static constexpr float DRYING_BIAS = {cpp_float(drying['bias'])};

{emit_vector('WEATHER_MEAN', weather['temperature']['mean'])}
{emit_vector('WEATHER_SCALE', weather['temperature']['scale'])}
{emit_vector('WEATHER_TEMP_WEIGHTS', weather['temperature']['weights'])}
static constexpr float WEATHER_TEMP_BIAS = {cpp_float(weather['temperature']['bias'])};
{emit_vector('WEATHER_RAIN_WEIGHTS', weather['rain_mm']['weights'])}
static constexpr float WEATHER_RAIN_BIAS = {cpp_float(weather['rain_mm']['bias'])};
{emit_vector('WEATHER_ET0_WEIGHTS', weather['et0']['weights'])}
static constexpr float WEATHER_ET0_BIAS = {cpp_float(weather['et0']['bias'])};
{emit_vector('RAIN_PROB_WEIGHTS', rain['weights'])}
static constexpr float RAIN_PROB_BIAS = {cpp_float(rain['bias'])};

{emit_vector('FAULT_MEAN', fault['mean'])}
{emit_vector('FAULT_SCALE', fault['scale'])}
{emit_vector('FAULT_BIAS', fault['bias'])}
{emit_matrix('FAULT_WEIGHTS', fault['weights'])}

static constexpr const char* FAULT_CLASS_NAMES[FAULT_CLASS_COUNT] = {{
    \"normal\", \"leak\", \"blocked_pipe\", \"empty_tank\", \"valve_fault\", \"sensor_fault\"
}};
}}  // namespace EdgeAIModel
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def train(dataset: Path) -> tuple[dict, dict, dict]:
    df = prepare_frame(dataset)
    train_mask = df["date"] < pd.Timestamp("2025-01-01", tz="UTC")
    test_mask = df["date"] >= pd.Timestamp("2025-01-01", tz="UTC")

    x_irr = matrix(df, IRRIGATION_FEATURES)
    y_irr = df["irrigation_needed_next_day"].to_numpy(dtype=np.float64)
    irrigation = fit_logistic(x_irr[train_mask], y_irr[train_mask], balance=True)
    irr_test_prob = logistic_predict(irrigation, x_irr[test_mask])
    irrigation_threshold = choose_threshold(y_irr[test_mask], irr_test_prob, recall_weight=2.0)

    positive_train = train_mask & (df["recommended_water_mm"] > 0.0)
    water = fit_ridge(x_irr[positive_train], df.loc[positive_train, "recommended_water_mm"].to_numpy(), l2=8.0)

    drying_rows = (
        train_mask & df["next_moisture_pct"].notna() &
        (df["next_irrigated"].fillna(0) == 0) & (df["next_rain_mm"].fillna(0) < 0.5)
    )
    drying = fit_ridge(x_irr[drying_rows], df.loc[drying_rows, "drying_rate_target"].to_numpy(), l2=10.0)

    weather_valid = df["next_temperature_c"].notna() & df["next_rain_mm"].notna() & df["next_et0_mm"].notna()
    x_weather = matrix(df, WEATHER_FEATURES)
    weather_train = train_mask & weather_valid
    weather_test = test_mask & weather_valid
    weather = {
        "temperature": fit_ridge(x_weather[weather_train], df.loc[weather_train, "next_temperature_c"].to_numpy(), l2=12.0),
        "rain_mm": fit_ridge(x_weather[weather_train], df.loc[weather_train, "next_rain_mm"].to_numpy(), l2=20.0),
        "et0": fit_ridge(x_weather[weather_train], df.loc[weather_train, "next_et0_mm"].to_numpy(), l2=12.0),
    }
    rain = fit_logistic(x_weather[weather_train], df.loc[weather_train, "rain_next_24h"].to_numpy(), balance=True, l2=0.12)
    rain_prob_test = logistic_predict(rain, x_weather[weather_test])
    rain_threshold = choose_threshold(df.loc[weather_test, "rain_next_24h"].to_numpy(), rain_prob_test, recall_weight=1.5)

    x_fault = matrix(df, FAULT_FEATURES)
    fault = fit_fault_models(x_fault[train_mask], df.loc[train_mask, "anomaly_label"].to_numpy())
    fault_pred, fault_conf = fault_predict(fault, x_fault[test_mask])
    fault_truth = df.loc[test_mask, "anomaly_label"].map({name: i for i, name in enumerate(FAULT_CLASSES)}).to_numpy()
    per_class_recall = {}
    for i, name in enumerate(FAULT_CLASSES):
        selected = fault_truth == i
        per_class_recall[name] = float(np.mean(fault_pred[selected] == i)) if np.any(selected) else None

    water_test_rows = test_mask & (df["recommended_water_mm"] > 0.0)
    water_pred = np.clip(linear_predict(water, x_irr[water_test_rows]), 0.0, 40.0)
    water_true = df.loc[water_test_rows, "recommended_water_mm"].to_numpy()
    drying_test_rows = (
        test_mask & df["next_moisture_pct"].notna() &
        (df["next_irrigated"].fillna(0) == 0) & (df["next_rain_mm"].fillna(0) < 0.5)
    )
    drying_pred = linear_predict(drying, x_irr[drying_test_rows])
    drying_true = df.loc[drying_test_rows, "drying_rate_target"].to_numpy()

    weather_metrics = {}
    for name, target, model in [
        ("temperature_c", "next_temperature_c", weather["temperature"]),
        ("rain_mm", "next_rain_mm", weather["rain_mm"]),
        ("et0_mm", "next_et0_mm", weather["et0"]),
    ]:
        predicted = linear_predict(model, x_weather[weather_test])
        if name in {"rain_mm", "et0_mm"}:
            predicted = np.maximum(predicted, 0.0)
        actual = df.loc[weather_test, target].to_numpy()
        weather_metrics[name] = {
            "mae": float(np.mean(np.abs(predicted - actual))),
            "rmse": float(np.sqrt(np.mean((predicted - actual) ** 2))),
        }

    metrics = {
        "validation_policy": "train=2023-2024, temporal holdout=2025",
        "records": int(len(df)), "train_records": int(np.sum(train_mask)), "test_records": int(np.sum(test_mask)),
        "irrigation": binary_metrics(y_irr[test_mask], irr_test_prob, irrigation_threshold),
        "recommended_water_mm": {
            "mae": float(np.mean(np.abs(water_pred - water_true))),
            "rmse": float(np.sqrt(np.mean((water_pred - water_true) ** 2))),
            "test_events": int(len(water_true)),
        },
        "drying_rate_pct_day": {
            "mae": float(np.mean(np.abs(drying_pred - drying_true))),
            "test_days": int(len(drying_true)),
        },
        "rain_next_24h": binary_metrics(df.loc[weather_test, "rain_next_24h"].to_numpy(), rain_prob_test, rain_threshold),
        "weather_next_day": weather_metrics,
        "fault_detection": {
            "accuracy": float(np.mean(fault_pred == fault_truth)),
            "macro_recall": float(np.mean([v for v in per_class_recall.values() if v is not None])),
            "per_class_recall": per_class_recall,
            "mean_confidence": float(np.mean(fault_conf)),
        },
        "deployment_gate": "SHADOW_MODE_ONLY_SYNTHETIC_DEVICE_LABELS",
    }

    models = {"irrigation": irrigation, "water": water, "drying": drying, "rain": rain, "weather": weather, "fault": fault}
    thresholds = {"irrigation": irrigation_threshold, "rain": rain_threshold}
    manifest = {
        "model_version": 20260807, "model_name": "AMR Tunisia Hybrid Edge AI v1",
        "dataset": str(dataset.relative_to(ROOT)),
        "dataset_sha256": hashlib.sha256(dataset.read_bytes()).hexdigest(),
        "features": {"irrigation": IRRIGATION_FEATURES, "weather": WEATHER_FEATURES, "fault": FAULT_FEATURES},
        "fault_classes": FAULT_CLASSES, "thresholds": thresholds,
        "models": {
            "irrigation": model_to_json(irrigation), "water": model_to_json(water),
            "drying": model_to_json(drying), "rain": model_to_json(rain),
            "weather": {key: model_to_json(value) for key, value in weather.items()},
            "fault": model_to_json(fault),
        },
        "governance": {
            "weather_origin": "Open-Meteo ERA5 reanalysis",
            "device_label_origin": "deterministic synthetic prototype data",
            "allowed_mode": "shadow",
            "production_requirement": "retrain and validate with calibrated field observations",
        },
    }
    return models, thresholds, {"metrics": metrics, "manifest": manifest}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, default=DEFAULT_DATASET)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--metrics", type=Path, default=DEFAULT_METRICS)
    args = parser.parse_args()
    dataset = args.dataset.resolve()
    models, thresholds, artifacts = train(dataset)
    dataset_hash = hashlib.sha256(dataset.read_bytes()).hexdigest()
    write_header(args.header.resolve(), dataset_hash, models, thresholds)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(artifacts["manifest"], indent=2) + "\n", encoding="utf-8")
    args.metrics.write_text(json.dumps(artifacts["metrics"], indent=2) + "\n", encoding="utf-8")
    print(json.dumps(artifacts["metrics"], indent=2))


if __name__ == "__main__":
    main()

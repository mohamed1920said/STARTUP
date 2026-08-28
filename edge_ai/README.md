# Edge-AI training tools

`train_edge_ai.py` trains the compact ESP32-S3 model bank from the Tunisia CSV,
writes `model_manifest.json` and `metrics.json`, and regenerates
`src/central/EdgeAIModel.h`.

`test_edge_ai.py` checks the training hash, model dimensions, dry-versus-wet
behaviour, finite weather forecasts and injected-fault recognition.

`preview_dashboard.py` is a localhost-only visual fixture server for dashboard
QA; it is never included in the ESP32 filesystem image.

See `docs/EDGE_AI.md` for architecture, metrics, safety gates and deployment.

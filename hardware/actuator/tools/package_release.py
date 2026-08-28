"""Create the deterministic AMR actuator prototype delivery package."""

from __future__ import annotations

import hashlib
import json
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
ACT = ROOT / "hardware" / "actuator"
OUT = ACT / "manufacturing"
OUT.mkdir(parents=True, exist_ok=True)
ZIP_PATH = OUT / "AMR_50mm_Valve_Actuator_Prototype_Package.zip"
MANIFEST_PATH = OUT / "MANIFEST.sha256"
SUMMARY_PATH = OUT / "PACKAGE_SUMMARY.json"


def wanted_files() -> list[Path]:
    files: list[Path] = []
    for directory, patterns in [
        (ACT / "cad" / "native", ("*.SLDPRT", "*.SLDASM")),
        (ACT / "cad" / "step", ("*.STEP",)),
        (ACT / "cad" / "stl", ("*.STL",)),
        (ACT / "cad" / "dxf", ("*.dxf",)),
        (ACT / "cad" / "renders", ("*.png", "*.svg")),
        (ACT / "cad" / "motion", ("*.gif", "*.png", "*.txt")),
        (ACT / "bom", ("*.csv", "*.json")),
        (ACT / "tools", ("*.py", "*.vbs", "*.ps1", "*.cs")),
    ]:
        for pattern in patterns:
            files.extend(sorted(directory.glob(pattern)))
    files.extend([
        ACT / "README.md",
        ACT / "ASSEMBLY_INSTRUCTIONS.md",
        ACT / "INTERFACE_CONTROL.md",
        ACT / "RELEASE_CHECKLIST.md",
        ROOT / "output" / "pdf" / "AMR_Valve_Actuator_Manufacturing_Pack.pdf",
    ])
    missing = [path for path in files if not path.exists()]
    if missing:
        raise FileNotFoundError("Missing package files: " + ", ".join(map(str, missing)))
    return sorted(set(files), key=lambda path: path.as_posix().lower())


def archive_name(path: Path) -> str:
    if path.is_relative_to(ACT):
        return (Path("AMR_Valve_Actuator") / path.relative_to(ACT)).as_posix()
    return (Path("AMR_Valve_Actuator") / "documentation" / path.name).as_posix()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    files = wanted_files()
    manifest_lines = [f"{sha256(path)}  {archive_name(path)}" for path in files]
    MANIFEST_PATH.write_text("\n".join(manifest_lines) + "\n", encoding="ascii")
    summary = {
        "release": "A-PROTOTYPE",
        "production_released": False,
        "solidworks_version": "2026 SP3.2 (34.3.2)",
        "native_parts_validated": 33,
        "resolved_assembly_components": 35,
        "motion_study": {
            "name": "AMR_Valve_Open_Close",
            "type": "Basic Motion",
            "duration_seconds": 6,
            "motor_features": 10,
            "reopen_validation": "PASS",
        },
        "pdf_pages": 20,
        "counts": {
            "native": len(list((ACT / "cad" / "native").glob("*"))),
            "step": len(list((ACT / "cad" / "step").glob("*.STEP"))),
            "stl": len(list((ACT / "cad" / "stl").glob("*.STL"))),
            "dxf": len(list((ACT / "cad" / "dxf").glob("*.dxf"))),
            "motion_previews": len(list((ACT / "cad" / "motion").glob("*.gif"))) + len(list((ACT / "cad" / "motion").glob("*.png"))),
        },
        "critical_holds": [
            "Measure real 5840-31ZY motor body, mounting, shaft, and stall current.",
            "Measure real valve neck/stem and breakaway torque in all required conditions.",
            "Implement continuous BTS7960 actuator firmware; current TB6612 pulse firmware is incompatible.",
            "Complete ingress, safety, EMC, environmental, and endurance testing before sale.",
        ],
    }
    SUMMARY_PATH.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    extra = [MANIFEST_PATH, SUMMARY_PATH]
    with zipfile.ZipFile(ZIP_PATH, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files + extra:
            archive.write(path, archive_name(path))
    print(f"{ZIP_PATH}\nSHA256 {sha256(ZIP_PATH)}")


if __name__ == "__main__":
    main()

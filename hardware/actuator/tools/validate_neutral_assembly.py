"""Check the placed neutral assembly for unintended solid intersections."""

from __future__ import annotations

import json
from pathlib import Path

import cadquery as cq

from generate_neutral_cad import ASSEMBLY_ITEMS, STEP, assembly_location


REPORT = Path(__file__).resolve().parents[1] / "manufacturing" / "ASSEMBLY_GEOMETRY_VALIDATION.json"


def bbox_overlap(a: cq.Shape, b: cq.Shape, tolerance: float = 0.02) -> bool:
    aa = a.BoundingBox()
    bb = b.BoundingBox()
    return (
        min(aa.xmax, bb.xmax) - max(aa.xmin, bb.xmin) > tolerance
        and min(aa.ymax, bb.ymax) - max(aa.ymin, bb.ymin) > tolerance
        and min(aa.zmax, bb.zmax) - max(aa.zmin, bb.zmin) > tolerance
    )


def main() -> None:
    shapes: list[tuple[str, cq.Shape]] = []
    occurrences: dict[str, int] = {}
    for name, x, y, z, orientation in ASSEMBLY_ITEMS:
        occurrences[name] = occurrences.get(name, 0) + 1
        instance = f"{name}_{occurrences[name]:02d}"
        source = cq.importers.importStep(str(STEP / f"{name}.STEP")).val()
        shapes.append((instance, source.moved(assembly_location(x, y, z, orientation))))

    intersections: list[dict[str, object]] = []
    for index, (name_a, shape_a) in enumerate(shapes):
        if "Cover" in name_a:
            continue
        for name_b, shape_b in shapes[index + 1 :]:
            if "Cover" in name_b or not bbox_overlap(shape_a, shape_b):
                continue
            common = shape_a.intersect(shape_b)
            volume = common.Volume()
            if volume > 0.05:
                intersections.append({"a": name_a, "b": name_b, "volume_mm3": round(volume, 3)})

    report = {
        "component_count": len(shapes),
        "unexpected_intersection_count": len(intersections),
        "unexpected_intersections": intersections,
        "cover_excluded_from_pair_check": True,
        "threshold_mm3": 0.05,
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    raise SystemExit(1 if intersections else 0)


if __name__ == "__main__":
    main()

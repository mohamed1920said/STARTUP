"""Generate exact neutral gear solids, laser DXFs, and verification previews.

Requires CadQuery 2.6.1. All dimensions are millimetres.
The SolidWorks generator imports the two STEP gear solids and adds project
metadata/global variables to the resulting native part documents.
"""

from __future__ import annotations

import math
from pathlib import Path

import cadquery as cq
from cadquery import exporters
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


ROOT = Path(__file__).resolve().parents[3]
CAD = ROOT / "hardware" / "actuator" / "cad"
STEP = CAD / "step"
STL = CAD / "stl"
DXF = CAD / "dxf"
RENDERS = CAD / "renders"
for directory in (STEP, STL, DXF, RENDERS):
    directory.mkdir(parents=True, exist_ok=True)


def involute_gear(
    teeth: int,
    module: float,
    width: float,
    bore: float,
    *,
    d_flat: float | None = None,
    pressure_angle_deg: float = 20.0,
    mesh_backlash: float = 0.30,
    flank_points: int = 8,
) -> cq.Workplane:
    """Create a standard full-depth external spur gear with practical backlash.

    Backlash is split equally between the two mating gears. Root arcs are
    represented by short chords; active flanks follow the involute equation.
    """

    pa = math.radians(pressure_angle_deg)
    rp = module * teeth / 2.0
    rb = rp * math.cos(pa)
    ro = rp + module
    rr = rp - 1.25 * module
    pitch = 2.0 * math.pi / teeth
    # Each gear supplies half the requested mesh clearance.
    half_pitch_tooth = math.pi / (2.0 * teeth) - mesh_backlash / (4.0 * rp)
    inv_pa = math.tan(pa) - pa
    start_radius = max(rr, rb)

    def half_angle_at(radius: float) -> float:
        alpha = math.acos(rb / radius)
        involute = math.tan(alpha) - alpha
        return half_pitch_tooth + inv_pa - involute

    start_half = half_angle_at(start_radius)
    outer_half = half_angle_at(ro)
    points: list[tuple[float, float]] = []

    for tooth in range(teeth):
        center = tooth * pitch
        points.append((rr * math.cos(center - start_half), rr * math.sin(center - start_half)))
        if start_radius > rr + 1e-6:
            points.append(
                (start_radius * math.cos(center - start_half), start_radius * math.sin(center - start_half))
            )
        for index in range(flank_points + 1):
            radius = start_radius + (ro - start_radius) * index / flank_points
            angle = center - half_angle_at(radius)
            points.append((radius * math.cos(angle), radius * math.sin(angle)))
        points.append((ro * math.cos(center + outer_half), ro * math.sin(center + outer_half)))
        for index in range(flank_points - 1, -1, -1):
            radius = start_radius + (ro - start_radius) * index / flank_points
            angle = center + half_angle_at(radius)
            points.append((radius * math.cos(angle), radius * math.sin(angle)))
        if start_radius > rr + 1e-6:
            points.append((rr * math.cos(center + start_half), rr * math.sin(center + start_half)))

    cleaned: list[tuple[float, float]] = []
    for point in points:
        if not cleaned or math.dist(point, cleaned[-1]) > 1e-7:
            cleaned.append(point)
    if len(cleaned) > 2 and math.dist(cleaned[0], cleaned[-1]) < 1e-7:
        cleaned.pop()
    gear = cq.Workplane("XY").polyline(cleaned).close().extrude(width)
    if d_flat is None:
        bore_tool = cq.Workplane("XY").circle(bore / 2.0).extrude(width + 2.0)
    else:
        radius = bore / 2.0
        flat_x = radius - d_flat
        bore_points = []
        for index in range(96):
            angle = 2.0 * math.pi * index / 96
            bore_points.append((min(radius * math.cos(angle), flat_x), radius * math.sin(angle)))
        bore_tool = cq.Workplane("XY").polyline(bore_points).close().extrude(width + 2.0)
    return gear.cut(bore_tool)


def export_gear(name: str, gear: cq.Workplane) -> None:
    exporters.export(gear, str(STEP / f"{name}.STEP"))
    exporters.export(gear, str(STL / f"{name}.STL"), tolerance=0.03, angularTolerance=0.08)
    svg_options = {
        "width": 900,
        "height": 900,
        "marginLeft": 25,
        "marginTop": 25,
        "showAxes": False,
        "projectionDir": (0, 0, 1),
        "strokeWidth": 0.35,
    }
    exporters.export(gear, str(RENDERS / f"{name}_top.svg"), opt=svg_options)


def solidworks_top(shape: cq.Workplane) -> cq.Workplane:
    """Map an ordinary XY/+Z CadQuery part to the SW Top-Plane/+Y convention."""

    return shape.rotate((0, 0, 0), (1, 0, 0), -90)


def export_corrected_part(name: str, shape: cq.Workplane, *, printed: bool = False) -> None:
    sw_shape = solidworks_top(shape)
    exporters.export(sw_shape, str(STEP / f"{name}.STEP"))
    if printed:
        exporters.export(sw_shape, str(STL / f"{name}.STL"), tolerance=0.05, angularTolerance=0.12)


def generate_corrected_parts() -> None:
    """Generate the Rev-B fit corrections independent of a running SW session."""

    motor_bracket = (
        cq.Workplane("XY")
        .rect(104, 86)
        .circle(30)
        .pushPoints([(-44, -30), (-44, 30), (44, -30), (44, 30)])
        .rect(12, 6.5)
        .extrude(6)
    )
    export_corrected_part("03_Motor_Bracket", motor_bracket)

    lower_base = (
        cq.Workplane("XY")
        .rect(240, 180)
        .pushPoints([(45, 0)])
        .circle(47.1 / 2)
        .pushPoints([(-45, 0)])
        .circle(30)
        .pushPoints([(-105, -75), (105, -75), (-105, 75), (105, 75)])
        .circle(8.5 / 2)
        .pushPoints([(-60, -38), (-60, 38), (-30, -38), (-30, 38)])
        .circle(6.5 / 2)
        .extrude(6)
    )
    export_corrected_part("01_Lower_Base_Plate", lower_base)

    shaft = cq.Workplane("XY").circle(10).extrude(161)
    flange = cq.Workplane("XY").circle(22).extrude(10)
    for px, py in [(-15, 0), (15, 0), (0, -15), (0, 15)]:
        flange = flange.cut(cq.Workplane("XY").center(px, py).circle(3.3).extrude(11))
    export_corrected_part("06_Output_Shaft", shaft.union(flange))

    hub = cq.Workplane("XY").circle(25).extrude(6).union(cq.Workplane("XY").circle(15).extrude(26))
    hub = hub.cut(cq.Workplane("XY").circle(10.05).extrude(27))
    export_corrected_part("07_Output_Gear_Hub", hub)

    export_corrected_part("08_Clutch_Pressure_Plate", cq.Workplane("XY").circle(31).circle(10.2).extrude(5))
    export_corrected_part("09_Belleville_Stack_Reference", cq.Workplane("XY").circle(17).circle(10.2).extrude(12))

    walls = cq.Workplane("XY").rect(270, 210).rect(256, 196).extrude(170)
    roof = cq.Workplane("XY").rect(270, 210).extrude(6)
    export_corrected_part("25_Gear_Enclosure_Cover", walls.union(roof), printed=True)


def dxf_header() -> list[str]:
    return ["0", "SECTION", "2", "HEADER", "9", "$INSUNITS", "70", "4", "0", "ENDSEC", "0", "SECTION", "2", "ENTITIES"]


def dxf_line(lines: list[str], x1: float, y1: float, x2: float, y2: float, layer: str = "CUT") -> None:
    lines.extend(["0", "LINE", "8", layer, "10", f"{x1:.4f}", "20", f"{y1:.4f}", "11", f"{x2:.4f}", "21", f"{y2:.4f}"])


def dxf_circle(lines: list[str], x: float, y: float, diameter: float, layer: str = "CUT") -> None:
    lines.extend(["0", "CIRCLE", "8", layer, "10", f"{x:.4f}", "20", f"{y:.4f}", "40", f"{diameter / 2:.4f}"])


def dxf_rectangle(lines: list[str], width: float, height: float, cx: float = 0, cy: float = 0, layer: str = "CUT") -> None:
    x1, x2 = cx - width / 2, cx + width / 2
    y1, y2 = cy - height / 2, cy + height / 2
    dxf_line(lines, x1, y1, x2, y1, layer)
    dxf_line(lines, x2, y1, x2, y2, layer)
    dxf_line(lines, x2, y2, x1, y2, layer)
    dxf_line(lines, x1, y2, x1, y1, layer)


def write_plate_dxf(name: str, width: float, height: float, circles=(), rectangles=()) -> None:
    lines = dxf_header()
    dxf_rectangle(lines, width, height)
    for x, y, diameter in circles:
        dxf_circle(lines, x, y, diameter)
    for cx, cy, rect_width, rect_height in rectangles:
        dxf_rectangle(lines, rect_width, rect_height, cx, cy)
    lines.extend(["0", "ENDSEC", "0", "EOF"])
    (DXF / f"{name}.dxf").write_text("\n".join(lines) + "\n", encoding="ascii")


def generate_dxf_set() -> None:
    write_plate_dxf(
        "01_Lower_Base_Plate",
        240,
        180,
        circles=[(45, 0, 47.1), (-45, 0, 60), (-105, -75, 8.5), (105, -75, 8.5), (-105, 75, 8.5), (105, 75, 8.5), (-60, -38, 6.5), (-60, 38, 6.5), (-30, -38, 6.5), (-30, 38, 6.5)],
    )
    write_plate_dxf(
        "02_Upper_Support_Plate",
        210,
        160,
        circles=[(45, 0, 47.1), (-90, -65, 8.5), (90, -65, 8.5), (-90, 65, 8.5), (90, 65, 8.5), (29, -22, 4.5), (61, -22, 4.5), (29, 22, 4.5), (61, 22, 4.5)],
    )
    write_plate_dxf("03_Motor_Bracket", 104, 86, circles=[(0, 0, 60)], rectangles=[(-44, -30, 12, 6.5), (-44, 30, 12, 6.5), (44, -30, 12, 6.5), (44, 30, 12, 6.5)])
    write_plate_dxf("13_AS5600_Bracket", 64, 52, circles=[(0, 0, 10), (-22, -16, 4), (22, -16, 4), (-22, 16, 4), (22, 16, 4)])
    for state in ("OPEN", "CLOSED"):
        write_plate_dxf(f"Microswitch_Bracket_{state}", 70, 32, rectangles=[(-17.5, 0, 14, 5.5), (17.5, 0, 14, 5.5)])
    write_plate_dxf("20_Mechanical_Stop_Bracket", 112, 34, circles=[(-42, 0, 8.5), (42, 0, 8.5), (-22, 0, 8.5), (22, 0, 8.5)])
    write_plate_dxf("11_Valve_Clamp_50mm", 168, 70, circles=[(-70, -22, 8.5), (70, -22, 8.5), (-70, 22, 8.5), (70, 22, 8.5)], rectangles=[(0, 0, 72, 46)])
    write_plate_dxf("12_Valve_Clamp_63mm", 168, 70, circles=[(-70, -22, 8.5), (70, -22, 8.5), (-70, 22, 8.5), (70, 22, 8.5)], rectangles=[(0, 0, 88, 46)])


# Part files are created on the SOLIDWORKS Top Plane.  Their local +Y axis is
# therefore the extrusion/stack axis, while this assembly uses conventional
# mechanical coordinates (X/Y horizontal, Z up).  "top" applies the required
# +90 degree X rotation.  The cover is inverted so its roof is at the top and
# the cells are rotated onto the battery tray.
ASSEMBLY_ITEMS = [
    ("01_Lower_Base_Plate", 0, 0, 0, "top"),
    ("02_Upper_Support_Plate", 0, 0, 81, "top"),
    ("03_Motor_Bracket", -45, 0, 7, "top"),
    ("04_Motor_Gear_40T", -45, 0, 29.5, "native"),
    ("05_Output_Gear_80T", 45, 0, 28, "gear_phase_native"),
    ("06_Output_Shaft", 45, 0, -34, "top"),
    ("07_Output_Gear_Hub", 45, 0, 22, "top"),
    ("08_Clutch_Pressure_Plate", 45, 0, 48, "top"),
    ("09_Belleville_Stack_Reference", 45, 0, 53, "top"),
    ("10_Valve_Stem_Adapter", 45, 0, -52, "top"),
    ("11_Valve_Clamp_50mm", 45, 0, -66, "top"),
    ("13_AS5600_Bracket", 45, 0, 139, "top"),
    ("14_Magnet_Carrier", 45, 0, 127, "top"),
    ("15_Reed_Switch_Cam", 45, 0, 87, "top"),
    ("16_Reed_Bracket_OPEN", 88, 38, 102, "top"),
    ("17_Reed_Bracket_CLOSED", 88, -38, 102, "top"),
    ("18_Microswitch_Bracket_OPEN", 100, 58, 97, "top"),
    ("19_Microswitch_Bracket_CLOSED", 100, -58, 97, "top"),
    ("20_Mechanical_Stop_Bracket", 45, -65, 87, "top"),
    ("21_Battery_Tray", -75, 58, 106, "top"),
    ("22_TTGO_Tray", -58, -48, 106, "top"),
    ("23_Motor_Driver_Tray", 68, 52, 106, "top"),
    ("24_Manual_Override", 45, 0, 97, "top"),
    ("25_Gear_Enclosure_Cover", 0, 0, 170, "cover"),
    ("26_Lower_Bearing_Carrier", 45, 0, 6, "top"),
    ("27_Upper_Bearing_Carrier", 45, 0, 65, "top"),
    ("REF_6204_2RS_Bearing", 45, 0, 7, "top"),
    ("REF_6204_2RS_Bearing", 45, 0, 66, "top"),
    ("REF_5840_31ZY_Motor", -45, 0, -44, "top"),
    ("REF_AS5600_PCB", 45, 0, 144, "top"),
    ("REF_TTGO_LORA32", -58, -48, 111, "top"),
    ("REF_BTS7960_IBT2", 68, 52, 111, "top"),
    ("REF_18650_Cell", -107.7, 34, 121, "cell_x"),
    ("REF_18650_Cell", -107.7, 58, 121, "cell_x"),
    ("REF_18650_Cell", -107.7, 82, 121, "cell_x"),
]


def assembly_location(x: float, y: float, z: float, orientation: str) -> cq.Location:
    if orientation == "native":
        return cq.Location(cq.Vector(x, y, z))
    if orientation == "top":
        return cq.Location(cq.Vector(x, y, z), cq.Vector(1, 0, 0), 90)
    if orientation == "gear_phase_native":
        return cq.Location(cq.Vector(x, y, z), cq.Vector(0, 0, 1), 2.25)
    if orientation == "cover":
        return cq.Location(cq.Vector(x, y, z), cq.Vector(1, 0, 0), -90)
    if orientation == "cell_x":
        return cq.Location(cq.Vector(x, y, z), cq.Vector(0, 0, 1), -90)
    raise ValueError(f"Unknown assembly orientation: {orientation}")


def build_assembly() -> None:
    assembly = cq.Assembly(name="AMR_50mm_Valve_Actuator")
    loaded: list[tuple[str, cq.Shape, tuple[float, float, float]]] = []
    occurrence: dict[str, int] = {}
    for base_name, x, y, z, orientation in ASSEMBLY_ITEMS:
        part_path = STEP / f"{base_name}.STEP"
        if not part_path.exists():
            raise FileNotFoundError(part_path)
        part = cq.importers.importStep(str(part_path))
        occurrence[base_name] = occurrence.get(base_name, 0) + 1
        instance_name = f"{base_name}_{occurrence[base_name]:02d}"
        location = assembly_location(x, y, z, orientation)
        assembly.add(part, name=instance_name, loc=location)
        loaded.append((instance_name, part.val().moved(location), (x, y, z)))

    assembly.save(str(STEP / "AMR_50mm_Valve_Actuator.STEP"), mode="default")
    render_assembly(loaded, RENDERS / "AMR_50mm_Valve_Actuator.png", exploded=False)
    render_assembly(loaded, RENDERS / "AMR_50mm_Valve_Actuator_exploded.png", exploded=True)


def render_assembly(
    loaded: list[tuple[str, cq.Shape, tuple[float, float, float]]],
    output: Path,
    *,
    exploded: bool,
) -> None:
    colors = ["#166534", "#16a34a", "#65a30d", "#0f766e", "#2563eb", "#ca8a04", "#64748b", "#dc2626"]
    fig = plt.figure(figsize=(12, 9), dpi=170)
    ax = fig.add_subplot(111, projection="3d")
    all_xyz: list[tuple[float, float, float]] = []

    for index, (name, shape, (x, y, z)) in enumerate(loaded):
        ex = ey = ez = 0.0
        if exploded:
            if "Cover" in name:
                ez = 105
            elif "Valve_Clamp" in name or "Stem_Adapter" in name:
                ez = -35
            elif "Battery" in name or "TTGO" in name or "BTS" in name or "18650" in name or "Motor_Driver" in name:
                ex = -18 if x < 0 else 18
                ey = 18 if y >= 0 else -18
                ez = 55
            elif "AS5600" in name or "Magnet" in name or "Manual" in name:
                ez = 45
            elif "Upper" in name:
                ez = 25
            elif "Gear" in name or "Clutch" in name or "Belleville" in name:
                ez = index * 1.5
        vertices, triangles = shape.tessellate(0.9)
        xyz = [(vertex.x + ex, vertex.y + ey, vertex.z + ez) for vertex in vertices]
        all_xyz.extend(xyz)
        faces = [[xyz[a], xyz[b], xyz[c]] for a, b, c in triangles]
        alpha = 0.08 if "Cover" in name else (0.42 if "Plate" in name else 0.88)
        mesh = Poly3DCollection(faces, facecolor=colors[index % len(colors)], edgecolor="#263238", linewidth=0.05, alpha=alpha)
        ax.add_collection3d(mesh)

    xs, ys, zs = zip(*all_xyz)
    ax.set_xlim(min(xs), max(xs))
    ax.set_ylim(min(ys), max(ys))
    ax.set_zlim(min(zs), max(zs))
    ax.set_box_aspect((max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))
    ax.view_init(elev=24, azim=-56)
    ax.set_xlabel("X (mm)")
    ax.set_ylabel("Y (mm)")
    ax.set_zlabel("Z (mm)")
    ax.set_title("AMR 50 mm Valve Actuator - " + ("Exploded Arrangement" if exploded else "Assembly Envelope"), fontsize=15, weight="bold")
    ax.grid(True, alpha=0.25)
    fig.tight_layout()
    fig.savefig(output, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    motor = involute_gear(40, 1.5, 17, 8.1, d_flat=0.8)
    output = involute_gear(80, 1.5, 20, 30.2)
    export_gear("04_Motor_Gear_40T", motor)
    export_gear("05_Output_Gear_80T", output)
    generate_corrected_parts()
    generate_dxf_set()
    build_assembly()
    print(f"Generated exact gear solids and DXFs under {CAD}")


if __name__ == "__main__":
    main()

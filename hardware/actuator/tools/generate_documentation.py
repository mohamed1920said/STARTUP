"""Generate actuator BOM files and the verified PDF manufacturing pack."""

from __future__ import annotations

import csv
import json
import math
import textwrap
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A3, landscape
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas
from reportlab.platypus import Table, TableStyle


ROOT = Path(__file__).resolve().parents[3]
ACT = ROOT / "hardware" / "actuator"
BOM_DIR = ACT / "bom"
PDF_DIR = ROOT / "output" / "pdf"
RENDERS = ACT / "cad" / "renders"
BOM_DIR.mkdir(parents=True, exist_ok=True)
PDF_DIR.mkdir(parents=True, exist_ok=True)

PDF_PATH = PDF_DIR / "AMR_Valve_Actuator_Manufacturing_Pack.pdf"
PAGE_W, PAGE_H = landscape(A3)
GREEN = colors.HexColor("#166534")
DARK = colors.HexColor("#17202A")
GREY = colors.HexColor("#5D6D7E")
LIGHT = colors.HexColor("#EAF4EC")
RED = colors.HexColor("#A61B1B")


MECHANICAL_BOM = [
    ("P01", "Lower base plate", 1, "6061-T6", "Laser/CNC + anodize", "240 x 180 x 6 mm"),
    ("P02", "Upper support plate", 1, "6061-T6", "Laser/CNC + anodize", "210 x 160 x 6 mm"),
    ("P03", "Adjustable motor bracket", 1, "6061-T6", "Laser/CNC + anodize", "+/-5 mm mesh adjustment"),
    ("P04", "40T motor gear", 1, "PA12-CF", "FDM + metal D insert", "m1.5, PA20, 17 mm face"),
    ("P05", "80T clutch gear", 1, "PA12-CF", "FDM + face finish", "m1.5, PA20, 20 mm face"),
    ("P06", "20 mm output shaft/flange", 1, "4140 or 316 SS", "Turn/mill/grind", "20 h6 journals; 4 x M6 flange"),
    ("P07", "Output gear clutch hub", 1, "C45 steel", "Turn/keyway/zinc", "OD50, bore20.1"),
    ("P08", "Clutch pressure plate", 1, "C45 steel", "Turn/grind/zinc", "OD62, bore17.2, t5"),
    ("P09", "Belleville stack", 1, "Spring steel", "Purchased/calibrated", "Initial preload about 530 N"),
    ("P10", "Replaceable stem adapter", 1, "17-4PH or PA12-CF", "Machine/print", "44 x 18; stem HOLD"),
    ("P11", "50 mm valve bridge clamp", 1, "6061-T6", "CNC + anodize", "72 mm HOLD opening"),
    ("P12", "63 mm alternative clamp", 1, "6061-T6", "CNC + anodize", "88 mm HOLD opening; alternative"),
    ("P13", "AS5600 bracket", 1, "6061 or PA12-CF", "Machine/print", "Nonmagnetic, adjustable"),
    ("P14", "Magnet carrier", 1, "PA12-CF", "FDM", "Final shaft mounting"),
    ("P15", "Reed cam ring", 1, "PA12-CF", "FDM", "Two adjustable magnets"),
    ("P16", "OPEN reed bracket", 1, "316 SS", "Laser", "Slotted"),
    ("P17", "CLOSED reed bracket", 1, "316 SS", "Laser", "Slotted"),
    ("P18", "OPEN microswitch bracket", 1, "316 SS", "Laser", "+/-5 deg adjustment"),
    ("P19", "CLOSED microswitch bracket", 1, "316 SS", "Laser", "+/-5 deg adjustment"),
    ("P20", "Mechanical stop bracket", 1, "6061 or steel", "Machine", "Two M8 stops"),
    ("P21", "3S1P battery tray", 1, "ASA/PA12-CF", "FDM", "3 x protected 18650 envelope"),
    ("P22", "TTGO tray", 1, "ASA/PA12-CF", "FDM", "USB service access"),
    ("P23", "BTS7960 tray", 1, "ASA/PA12-CF", "FDM", "25 mm heatsink clearance"),
    ("P24", "Manual override hex", 1, "316 SS", "Turn/mill/passivate", "30 AF, bore20.2, cross-pin"),
    ("P25", "Gasketed enclosure cover", 1, "UV PC/ASA", "FDM/mould", "IP65/IP67 design intent"),
    ("P26", "Lower bearing carrier", 1, "6061-T6", "Turn/mill/anodize", "47 H7 x 16; retainers"),
    ("P27", "Upper bearing carrier", 1, "6061-T6", "Turn/mill/anodize", "47 H7 x 16; retainers"),
]

MATERIAL_BOM = [
    ("M01", "5840-31ZY double-shaft gearmotor", 1, "12 V, 40 rpm nominal", "HOLD - measure supplier unit"),
    ("M02", "6204-2RS sealed bearing", 2, "20 x 47 x 14 mm", "Known standard"),
    ("M03", "AS5600 angle module", 1, "3.3 V I2C", "Measure PCB and magnet gap"),
    ("M04", "Diametric magnet", 1, "6 x 2.5 mm target", "Match AS5600 vendor guidance"),
    ("M05", "Reed switch", 2, "Sealed dry contact", "OPEN/CLOSED confirmation"),
    ("M06", "Reed cam magnet", 2, "Small NdFeB, retained", "Pot and mechanically capture"),
    ("M07", "Long-lever microswitch", 2, "C/NO/NC, 250 VAC 5 A", "Used at low-current control level"),
    ("M08", "Manual interlock switch", 1, "Two-pole NC + auxiliary", "Hardware driver disable"),
    ("M09", "TTGO LoRa32 v2.1", 1, "868 MHz", "Antenna separated from motor"),
    ("M10", "BTS7960 / IBT-2", 1, "High-current H bridge", "Qualify against measured stall current"),
    ("M11", "MCP23017 input expander", 1, "3.3 V I2C", "Reeds, limits, manual input"),
    ("M12", "3S BMS", 1, "Balancing + overcurrent", "Rating from measured current"),
    ("M13", "Protected 18650 cell", 3, "Matched cells, 3S1P", "12.6 V maximum"),
    ("M14", "Fuse and holder", 1, "DC rated", "Size after motor-current test"),
    ("M15", "Logic DC/DC converter", 1, "12.6 V input to stable logic rail", "Automotive/transient capable"),
    ("M16", "EPDM clamp liner", 1, "3 mm sheet", "Protect PVC body"),
    ("M17", "EPDM enclosure gasket", 1, "Closed-cell", "Compression verified by ingress test"),
    ("M18", "Cable glands", 4, "IP68, sized to cables", "Strain relief"),
    ("M19", "Hydrophobic vent", 1, "IP67 pressure equalization", "Above drainage plane"),
    ("M20", "Friction washers", 2, "Oil/water-stable friction material", "Torque test wet and dry"),
    ("M21", "Bronze/steel gear sleeve", 1, "30.0 mm nominal interface", "Final tolerance after wear test"),
]

FASTENER_BOM = [
    ("F01", "M3 x 8 A4 socket screw", 12, "Electronics and sensor PCBs"),
    ("F02", "M3 A4 Nyloc + washer", 12, "Electronics"),
    ("F03", "M4 x 12 A4 socket screw", 8, "Sensor/reed brackets"),
    ("F04", "M4 A4 Nyloc + washer", 8, "Sensor/reed brackets"),
    ("F05", "M5 x 16 A4 socket screw", 8, "Microswitch and small structure"),
    ("F06", "M5 A4 Nyloc + washer", 8, "Small structure"),
    ("F07", "M6 x 20 A4 socket screw", 16, "Motor, carriers, shaft flange"),
    ("F08", "M6 A4 Nyloc + washer", 16, "Motor, carriers, shaft flange"),
    ("F09", "M8 x 70 A4 bolt", 4, "Valve bridge clamp; confirm length"),
    ("F10", "M8 A4 Nyloc + large washer", 4, "Valve bridge clamp"),
    ("F11", "M8 x 35 steel stop screw", 2, "Mechanical stops"),
    ("F12", "M8 jam nut", 4, "Two per stop"),
    ("F13", "M16 prevailing-torque nut", 1, "Clutch preload"),
    ("F14", "M16 Belleville washers", 1, "Select stack for about 530 N initial preload"),
    ("F15", "6 x 6 mm parallel key", 1, "Output hub; length per hub"),
    ("F16", "5 mm stainless cross-pin", 1, "Manual override"),
    ("F17", "47 mm internal bearing retainer", 4, "Two per bearing carrier or drawing-equivalent plates"),
    ("F18", "M4 A4 enclosure screw", 12, "Gasketed cover; captive preferred"),
]

PARAMETERS = {
    "ValveDiameter_mm": 50,
    "MotorGearTeeth": 40,
    "OutputGearTeeth": 80,
    "GearModule_mm": 1.5,
    "PressureAngle_deg": 20,
    "GearCenterDistance_mm": 90,
    "MeshBacklash_mm": 0.30,
    "GearRatio": 2.0,
    "OutputShaftDiameter_mm": 20,
    "BearingOD_mm": 47,
    "BearingWidth_mm": 14,
    "PlateThickness_mm": 6,
    "ValveRotation_deg": 90,
    "NominalMotorRPM": 40,
    "NominalOutputRPM": 20,
    "Theoretical90DegreeTime_s": 0.75,
    "InitialClutchTorque_Nm": 8,
    "AssumedFrictionCoefficient": 0.30,
    "AssumedMeanFrictionRadius_mm": 25,
    "CalculatedInitialAxialPreload_N": 533,
}


def write_csv(path: Path, header: tuple[str, ...], rows: list[tuple]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        writer.writerows(rows)


def prepare_data() -> None:
    write_csv(BOM_DIR / "mechanical_bom.csv", ("Item", "Description", "Qty", "Material", "Process", "Critical note"), MECHANICAL_BOM)
    write_csv(BOM_DIR / "material_electrical_bom.csv", ("Item", "Description", "Qty", "Specification", "Release note"), MATERIAL_BOM)
    write_csv(BOM_DIR / "fastener_bom.csv", ("Item", "Description", "Qty", "Use"), FASTENER_BOM)
    write_csv(BOM_DIR / "design_parameters.csv", ("Parameter", "Value"), list(PARAMETERS.items()))
    (BOM_DIR / "calculations.json").write_text(json.dumps(PARAMETERS, indent=2) + "\n", encoding="utf-8")


def footer(c: canvas.Canvas, page: int, sheet: str = "") -> None:
    c.setStrokeColor(DARK)
    c.rect(10 * mm, 10 * mm, PAGE_W - 20 * mm, PAGE_H - 20 * mm)
    c.setFillColor(DARK)
    c.setFont("Helvetica", 7)
    c.drawString(14 * mm, 5.5 * mm, "AMR Valve Actuator | Rev A-PROTOTYPE | NOT RELEASED FOR PRODUCTION")
    c.drawRightString(PAGE_W - 14 * mm, 5.5 * mm, f"Sheet {page} {sheet}")


def title(c: canvas.Canvas, text: str, subtitle: str = "") -> None:
    c.setFillColor(GREEN)
    c.setFont("Helvetica-Bold", 23)
    c.drawString(18 * mm, PAGE_H - 25 * mm, text)
    if subtitle:
        c.setFillColor(GREY)
        c.setFont("Helvetica", 10)
        c.drawString(18 * mm, PAGE_H - 33 * mm, subtitle)


def wrapped(c: canvas.Canvas, text: str, x: float, y: float, width_chars: int = 95, leading: float = 5.0 * mm, font_size: int = 9, color=DARK) -> float:
    c.setFont("Helvetica", font_size)
    c.setFillColor(color)
    for paragraph in text.split("\n"):
        for line in textwrap.wrap(paragraph, width_chars) or [""]:
            c.drawString(x, y, line)
            y -= leading
        y -= 1.5 * mm
    return y


def draw_table(c: canvas.Canvas, data: list[list[str]], x: float, y_top: float, width: float, row_height: float = 7 * mm, font_size: float = 7.2) -> float:
    cols = len(data[0])
    col_widths = [width / cols] * cols
    table = Table(data, colWidths=col_widths, rowHeights=[row_height] * len(data))
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), GREEN),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("FONTNAME", (0, 1), (-1, -1), "Helvetica"),
        ("FONTSIZE", (0, 0), (-1, -1), font_size),
        ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#839192")),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("BACKGROUND", (0, 1), (-1, -1), colors.white),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, LIGHT]),
        ("LEFTPADDING", (0, 0), (-1, -1), 3),
        ("RIGHTPADDING", (0, 0), (-1, -1), 3),
    ]))
    height = row_height * len(data)
    table.wrapOn(c, width, height)
    table.drawOn(c, x, y_top - height)
    return y_top - height


def drawing_title_block(c: canvas.Canvas, page: int, part: str, material: str, finish: str, qty: str = "1") -> None:
    footer(c, page, part)
    x, y, w, h = 215 * mm, 12 * mm, 192 * mm, 28 * mm
    c.setStrokeColor(DARK)
    c.rect(x, y, w, h)
    c.line(x + 115 * mm, y, x + 115 * mm, y + h)
    c.line(x, y + 10 * mm, x + w, y + 10 * mm)
    c.line(x, y + 19 * mm, x + w, y + 19 * mm)
    c.setFillColor(DARK)
    c.setFont("Helvetica-Bold", 11)
    c.drawString(x + 3 * mm, y + 21.5 * mm, part)
    c.setFont("Helvetica", 7)
    c.drawString(x + 3 * mm, y + 13 * mm, f"Material: {material}")
    c.drawString(x + 3 * mm, y + 3.5 * mm, f"Finish: {finish}")
    c.drawString(x + 119 * mm, y + 21.5 * mm, "REV: A-PROTOTYPE")
    c.drawString(x + 119 * mm, y + 13 * mm, f"QTY: {qty}   UNITS: mm")
    c.drawString(x + 119 * mm, y + 3.5 * mm, "GENERAL: ISO 2768-m; deburr")


def dim_h(c: canvas.Canvas, x1: float, x2: float, y: float, text: str) -> None:
    c.setStrokeColor(GREY)
    c.setFillColor(DARK)
    c.setLineWidth(0.4)
    c.line(x1, y, x2, y)
    c.line(x1, y - 2 * mm, x1, y + 2 * mm)
    c.line(x2, y - 2 * mm, x2, y + 2 * mm)
    c.line(x1, y, x1 + 2 * mm, y + 1 * mm)
    c.line(x1, y, x1 + 2 * mm, y - 1 * mm)
    c.line(x2, y, x2 - 2 * mm, y + 1 * mm)
    c.line(x2, y, x2 - 2 * mm, y - 1 * mm)
    c.setFont("Helvetica", 7)
    c.drawCentredString((x1 + x2) / 2, y + 1.5 * mm, text)


def dim_v(c: canvas.Canvas, x: float, y1: float, y2: float, text: str) -> None:
    c.setStrokeColor(GREY)
    c.line(x, y1, x, y2)
    c.line(x - 2 * mm, y1, x + 2 * mm, y1)
    c.line(x - 2 * mm, y2, x + 2 * mm, y2)
    c.saveState()
    c.setFillColor(DARK)
    c.setFont("Helvetica", 7)
    c.translate(x + 2.5 * mm, (y1 + y2) / 2)
    c.rotate(90)
    c.drawCentredString(0, 0, text)
    c.restoreState()


def plan_view(c: canvas.Canvas, cx: float, cy: float, width_mm: float, height_mm: float, scale: float, holes=(), slots=()) -> tuple[float, float, float, float]:
    w, h = width_mm * scale * mm, height_mm * scale * mm
    x0, y0 = cx - w / 2, cy - h / 2
    c.setLineWidth(0.8)
    c.setStrokeColor(DARK)
    c.rect(x0, y0, w, h)
    for x, y, dia in holes:
        c.circle(cx + x * scale * mm, cy + y * scale * mm, dia * scale * mm / 2)
    for x, y, sw, sh in slots:
        c.rect(cx + (x - sw / 2) * scale * mm, cy + (y - sh / 2) * scale * mm, sw * scale * mm, sh * scale * mm)
    return x0, y0, w, h


def notes(c: canvas.Canvas, lines: list[str], x: float = 18 * mm, y: float = 62 * mm) -> None:
    c.setFillColor(DARK)
    c.setFont("Helvetica-Bold", 8)
    c.drawString(x, y, "MANUFACTURING NOTES")
    c.setFont("Helvetica", 7)
    y -= 5 * mm
    for index, line in enumerate(lines, 1):
        c.drawString(x, y, f"{index}. {line}")
        y -= 4.5 * mm


def cover_page(c: canvas.Canvas, page: int) -> None:
    footer(c, page)
    c.setFillColor(GREEN)
    c.rect(10 * mm, PAGE_H - 72 * mm, PAGE_W - 20 * mm, 62 * mm, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont("Helvetica-Bold", 29)
    c.drawString(22 * mm, PAGE_H - 38 * mm, "AMR 50 mm PVC VALVE ACTUATOR")
    c.setFont("Helvetica", 15)
    c.drawString(22 * mm, PAGE_H - 52 * mm, "Manufacturing Drawing, BOM, Assembly, and Prototype Release Pack")
    c.setFillColor(DARK)
    c.setFont("Helvetica-Bold", 13)
    c.drawString(22 * mm, PAGE_H - 92 * mm, "REVISION A-PROTOTYPE - NOT RELEASED FOR PRODUCTION OR SALE")
    y = PAGE_H - 110 * mm
    facts = [
        "Motor: 5840-31ZY, 12 V, 40 rpm nominal, double shaft (real unit must be measured)",
        "Transmission: 40T/80T, module 1.5, PA20, 90.00 mm centre distance, 2:1 ratio",
        "Output: 20 mm shaft, dual 6204-2RS bearings, adjustable friction clutch, 30 mm manual hex",
        "Feedback: AS5600 on final shaft plus independent reeds and normally-closed hard limits",
        "Structure: 6 mm 6061-T6 plates plus 16 mm bearing carriers and replaceable valve clamp/adapter",
        "Protection intent: IP65 minimum / IP67 preferred after production-intent ingress testing",
    ]
    for fact in facts:
        c.setFillColor(GREEN)
        c.circle(25 * mm, y + 1.5 * mm, 1.2 * mm, fill=1, stroke=0)
        c.setFillColor(DARK)
        c.setFont("Helvetica", 11)
        c.drawString(32 * mm, y, fact)
        y -= 13 * mm
    c.setFillColor(colors.HexColor("#FFF2CC"))
    c.roundRect(22 * mm, 36 * mm, PAGE_W - 44 * mm, 34 * mm, 3 * mm, fill=1, stroke=0)
    c.setFillColor(RED)
    c.setFont("Helvetica-Bold", 11)
    c.drawString(29 * mm, 57 * mm, "CRITICAL HOLD")
    c.setFillColor(DARK)
    c.setFont("Helvetica", 9)
    c.drawString(29 * mm, 47 * mm, "Measure the actual motor, valve neck/stem, valve breakaway torque, and motor stall current before machining final interfaces.")
    c.drawString(29 * mm, 40 * mm, "The current repository TB6612 30 ms latching-valve firmware must not drive this continuous 12 V motor.")


def image_page(c: canvas.Canvas, page: int, heading: str, image: Path, caption: str) -> None:
    footer(c, page)
    title(c, heading)
    c.drawImage(str(image), 35 * mm, 27 * mm, width=350 * mm, height=235 * mm, preserveAspectRatio=True, anchor="c", mask="auto")
    c.setFillColor(GREY)
    c.setFont("Helvetica", 8)
    c.drawCentredString(PAGE_W / 2, 18 * mm, caption)


def calculations_page(c: canvas.Canvas, page: int) -> None:
    footer(c, page)
    title(c, "Design Calculations and Protection Stack", "Values are prototype sizing checks, not certification calculations.")
    data = [
        ["Check", "Equation / input", "Result", "Release interpretation"],
        ["Gear ratio", "80 / 40", "2.00:1", "Output speed 20 rpm nominal"],
        ["Centre distance", "m(z1+z2)/2", "90.00 mm", "Fixed datum; motor slots tune backlash"],
        ["90 deg travel", "0.25 rev / 20 rpm", "0.75 s no-load", "Use controlled PWM ramp; verify loaded time"],
        ["Clutch preload", "T/(2 mu r), T=8 Nm, mu=.30, r=25 mm", "533 N", "Select/test Belleville stack; wet friction matters"],
        ["Gear tangential load", "T/r, T=8 Nm, r=60 mm", "133 N", "200 N at 12 Nm before impact factor"],
        ["20 mm shaft torsion", "16T/(pi d^3), T=12 Nm", "7.6 MPa", "Key, flange, pin, and fatigue govern"],
        ["Stop reaction", "T/L, T=12 Nm, L=45 mm", "267 N", "Use >=4x impact factor for bracket check"],
    ]
    draw_table(c, data, 18 * mm, PAGE_H - 48 * mm, PAGE_W - 36 * mm, row_height=11 * mm, font_size=8)
    y = 118 * mm
    c.setFillColor(GREEN)
    c.setFont("Helvetica-Bold", 12)
    c.drawString(20 * mm, y, "Protection sequence")
    y -= 13 * mm
    steps = ["AS5600 motion target", "Reed confirmation", "NC hard-limit interrupt", "Steel M8 stop", "Friction clutch slip"]
    x = 25 * mm
    for index, step in enumerate(steps):
        c.setFillColor(LIGHT)
        c.roundRect(x, y - 8 * mm, 66 * mm, 18 * mm, 3 * mm, fill=1, stroke=0)
        c.setFillColor(DARK)
        c.setFont("Helvetica-Bold", 8)
        c.drawCentredString(x + 33 * mm, y, step)
        if index < len(steps) - 1:
            c.setStrokeColor(GREEN)
            c.setLineWidth(1.3)
            c.line(x + 66 * mm, y + 1 * mm, x + 75 * mm, y + 1 * mm)
        x += 75 * mm
    wrapped(c, "Final clutch torque is 1.5 times the highest measured valve breakaway torque only when that setting remains below the verified safe torque of the PVC stem, adapter bolts, printed gear teeth, motor gearbox, and stop structure. Measure dry/wet and hot/cold values; do not release using catalogue assumptions.", 20 * mm, 67 * mm, 140, 5 * mm, 9)


def bom_pages(c: canvas.Canvas, start_page: int) -> int:
    page = start_page
    chunks = [MECHANICAL_BOM[:14], MECHANICAL_BOM[14:]]
    for idx, chunk in enumerate(chunks, 1):
        footer(c, page)
        title(c, f"Mechanical BOM ({idx}/2)")
        data = [["Item", "Description", "Qty", "Material", "Process", "Critical note"]] + [[str(x) for x in row] for row in chunk]
        draw_table(c, data, 16 * mm, PAGE_H - 42 * mm, PAGE_W - 32 * mm, row_height=13 * mm, font_size=7)
        c.showPage()
        page += 1
    footer(c, page)
    title(c, "Material and Electrical BOM")
    data = [["Item", "Description", "Qty", "Specification", "Release note"]] + [[str(x) for x in row] for row in MATERIAL_BOM]
    draw_table(c, data, 16 * mm, PAGE_H - 42 * mm, PAGE_W - 32 * mm, row_height=10.5 * mm, font_size=7)
    c.showPage()
    page += 1
    footer(c, page)
    title(c, "Fastener BOM")
    data = [["Item", "Description", "Qty", "Use"]] + [[str(x) for x in row] for row in FASTENER_BOM]
    draw_table(c, data, 22 * mm, PAGE_H - 45 * mm, PAGE_W - 44 * mm, row_height=11.5 * mm, font_size=7.5)
    c.showPage()
    return page + 1


def drawing_lower(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P01 LOWER BASE PLATE", "6061-T6, 6 mm", "Clear anodize; mask interfaces")
    title(c, "P01 Lower Base Plate")
    cx, cy, scale = 142 * mm, 160 * mm, 0.72
    holes = [(45, 0, 47.1), (-45, 0, 60), (-105, -75, 8.5), (105, -75, 8.5), (-105, 75, 8.5), (105, 75, 8.5), (-60, -38, 6.5), (-60, 38, 6.5), (-30, -38, 6.5), (-30, 38, 6.5)]
    x0, y0, w, h = plan_view(c, cx, cy, 240, 180, scale, holes)
    dim_h(c, x0, x0 + w, y0 - 9 * mm, "240 +/-0.2")
    dim_v(c, x0 - 9 * mm, y0, y0 + h, "180 +/-0.2")
    c.setFont("Helvetica", 8); c.drawString(255 * mm, 205 * mm, "Bearing-carrier locator: DIA47.1 at X=+45, Y=0")
    c.drawString(255 * mm, 195 * mm, "Corner holes: 4 x DIA8.5 at X=+/-105, Y=+/-75")
    c.drawString(255 * mm, 185 * mm, "Motor clearance: DIA60 at X=-45; 4 x DIA6.5 slots/holes")
    notes(c, ["Plate is not the bearing seat; bolt P26 carrier to this plate.", "Maintain output/motor gear datum centres at 90.00 mm.", "Flatness 0.3 mm over plate; break all edges 0.5 mm max."])


def drawing_upper(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P02 UPPER SUPPORT PLATE", "6061-T6, 6 mm", "Clear anodize; mask interfaces")
    title(c, "P02 Upper Support Plate")
    cx, cy, scale = 142 * mm, 160 * mm, 0.76
    holes = [(45, 0, 47.1), (-90, -65, 8.5), (90, -65, 8.5), (-90, 65, 8.5), (90, 65, 8.5), (29, -22, 4.5), (61, -22, 4.5), (29, 22, 4.5), (61, 22, 4.5)]
    x0, y0, w, h = plan_view(c, cx, cy, 210, 160, scale, holes)
    dim_h(c, x0, x0 + w, y0 - 9 * mm, "210 +/-0.2")
    dim_v(c, x0 - 9 * mm, y0, y0 + h, "160 +/-0.2")
    c.setFont("Helvetica", 8); c.drawString(252 * mm, 204 * mm, "Carrier locator: DIA47.1 at X=+45, Y=0")
    c.drawString(252 * mm, 194 * mm, "Structure holes: 4 x DIA8.5 at X=+/-90, Y=+/-65")
    c.drawString(252 * mm, 184 * mm, "AS5600 holes: 4 x DIA4.5 about output axis")
    notes(c, ["P27 bearing carrier mounts below this plate.", "AS5600 support must be nonmagnetic and vertically adjustable.", "Flatness 0.3 mm over plate."])


def drawing_motor(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P03 MOTOR BRACKET", "6061-T6, 6 mm", "Clear anodize")
    title(c, "P03 Adjustable Motor Bracket")
    cx, cy, scale = 150 * mm, 160 * mm, 1.25
    slots = [(-44, -30, 12, 6.5), (-44, 30, 12, 6.5), (44, -30, 12, 6.5), (44, 30, 12, 6.5)]
    x0, y0, w, h = plan_view(c, cx, cy, 104, 86, scale, [(0, 0, 60)], slots)
    dim_h(c, x0, x0 + w, y0 - 10 * mm, "104 +/-0.2")
    dim_v(c, x0 - 10 * mm, y0, y0 + h, "86 +/-0.2")
    c.setFont("Helvetica", 8); c.drawString(260 * mm, 205 * mm, "4 slots: 12 x 6.5")
    c.drawString(260 * mm, 195 * mm, "Slot centres: X=+/-44, Y=+/-30")
    c.drawString(260 * mm, 185 * mm, "Centre/body clearance: DIA60")
    notes(c, ["Slots provide approximately +/-5 mm backlash adjustment.", "Final motor mount holes/cradle walls depend on measured supplier unit.", "Set final shaft centre exactly 90.00 mm from output axis."])


def drawing_shaft(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P06 OUTPUT SHAFT AND FLANGE", "4140 steel or 316 SS", "Grind/passivate or zinc-nickel")
    title(c, "P06 Output Shaft and Adapter Flange")
    x, y, s = 90 * mm, 58 * mm, 1.10
    c.setStrokeColor(DARK); c.setLineWidth(1)
    c.rect(x, y, 20 * s * mm, 161 * s * mm)
    c.rect(x - 12 * s * mm, y, 44 * s * mm, 10 * s * mm)
    dim_v(c, x - 22 * mm, y, y + 161 * s * mm, "161 +/-0.1")
    dim_h(c, x, x + 20 * s * mm, y + 167 * s * mm, "DIA20 h6")
    dim_h(c, x - 12 * s * mm, x + 32 * s * mm, y - 9 * mm, "DIA44 flange")
    c.setFont("Helvetica", 8)
    c.drawString(205 * mm, 215 * mm, "Flange: t10, 4 x DIA6.6 on PCD30")
    c.drawString(205 * mm, 204 * mm, "Bearing journals: DIA20 h6; Ra <=0.8 um")
    c.drawString(205 * mm, 193 * mm, "Keyway: 6 mm, machine to hub/key standard")
    c.drawString(205 * mm, 182 * mm, "Manual cross-pin: DIA5 H11, locate after stack measurement")
    notes(c, ["Do not transfer torque by set screws alone.", "Maintain flange face runout <=0.05 mm to bearing journals.", "Confirm 1-2 mm axial clearance above real valve stem."])


def drawing_adapter(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P10 VALVE STEM ADAPTER", "17-4PH or reinforced polymer", "Passivate if metal")
    title(c, "P10 Replaceable Valve Stem Adapter")
    cx, cy, s = 145 * mm, 165 * mm, 2.25
    c.setStrokeColor(DARK); c.circle(cx, cy, 22 * s * mm)
    c.rect(cx - 7.15 * s * mm, cy - 5.15 * s * mm, 14.3 * s * mm, 10.3 * s * mm)
    for x, y in [(-15, 0), (15, 0), (0, -15), (0, 15)]:
        c.circle(cx + x * s * mm, cy + y * s * mm, 3.3 * s * mm)
    dim_h(c, cx - 22 * s * mm, cx + 22 * s * mm, cy - 31 * s * mm, "DIA44")
    c.setFont("Helvetica", 8)
    c.drawString(260 * mm, 208 * mm, "Thickness 18 +/-0.1")
    c.drawString(260 * mm, 197 * mm, "4 x DIA6.6 on PCD30")
    c.drawString(260 * mm, 186 * mm, "Prototype socket: 14.3 x 10.3 THROUGH")
    c.drawString(260 * mm, 175 * mm, "FINAL SOCKET: HOLD - MEASURE REAL STEM")
    notes(c, ["Use a new insert for each valve stem family.", "Provide 1-2 mm axial clearance; never preload PVC stem axially.", "Bolt to P06 flange with four locked M6 fasteners."])


def drawing_hub(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P07 CLUTCH OUTPUT HUB", "C45 steel", "Zinc-nickel; mask friction/key surfaces")
    title(c, "P07 Clutch Output Hub")
    cx, cy, s = 145 * mm, 165 * mm, 2.15
    c.setStrokeColor(DARK); c.circle(cx, cy, 25 * s * mm); c.circle(cx, cy, 10.05 * s * mm)
    dim_h(c, cx - 25 * s * mm, cx + 25 * s * mm, cy - 33 * s * mm, "DIA50")
    c.setFont("Helvetica", 8)
    c.drawString(260 * mm, 208 * mm, "Sleeve height 26; flange height 6")
    c.drawString(260 * mm, 197 * mm, "Bore DIA20.1 before keyway")
    c.drawString(260 * mm, 186 * mm, "Keyway: 6 mm to selected shaft fit")
    c.drawString(260 * mm, 175 * mm, "Sleeve DIA30; friction face Ra 1.6-3.2 um")
    notes(c, ["Use positive key or cross-bolt; set screws are retention only.", "Keep zinc and grease off friction face.", "Verify sleeve permits free gear rotation before clutch preload."])


def drawing_switch(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P18/P19 LIMIT SWITCH BRACKETS", "316 stainless, 4 mm", "Deburr/passivate", "2")
    title(c, "P18/P19 OPEN and CLOSED Microswitch Brackets")
    cx, cy, s = 155 * mm, 165 * mm, 2.1
    slots = [(-17.5, 0, 14, 5.5), (17.5, 0, 14, 5.5)]
    x0, y0, w, h = plan_view(c, cx, cy, 70, 32, s, slots=slots)
    dim_h(c, x0, x0 + w, y0 - 10 * mm, "70")
    dim_v(c, x0 - 10 * mm, y0, y0 + h, "32")
    c.setFont("Helvetica", 8)
    c.drawString(270 * mm, 203 * mm, "2 slots: 14 x 5.5")
    c.drawString(270 * mm, 192 * mm, "Slot centres: X=+/-17.5")
    c.drawString(270 * mm, 181 * mm, "Adjustment target: at least +/-5 deg")
    notes(c, ["Use long-lever C/NO/NC microswitches.", "Wire NC contact to interrupt hazardous motor direction in hardware.", "Mount so reverse motion remains possible at either limit."])


def drawing_as5600(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P13 AS5600 BRACKET", "6061 or PA12-CF, 4 mm", "Nonmagnetic")
    title(c, "P13 AS5600 Adjustable Bracket")
    cx, cy, s = 155 * mm, 165 * mm, 2.25
    holes = [(0, 0, 10), (-22, -16, 4), (22, -16, 4), (-22, 16, 4), (22, 16, 4)]
    x0, y0, w, h = plan_view(c, cx, cy, 64, 52, s, holes)
    dim_h(c, x0, x0 + w, y0 - 10 * mm, "64")
    dim_v(c, x0 - 10 * mm, y0, y0 + h, "52")
    c.setFont("Helvetica", 8)
    c.drawString(270 * mm, 203 * mm, "Centre clearance DIA10")
    c.drawString(270 * mm, 192 * mm, "4 x DIA4 at X=+/-22, Y=+/-16")
    c.drawString(270 * mm, 181 * mm, "Add vertical slots to suit measured module/gap")
    notes(c, ["Magnet must be diametrically magnetized and concentric with final shaft.", "Use nonmagnetic screws/support near sensor.", "Calibrate actual valve CLOSED/OPEN angles after assembly."])


def drawing_clamp(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P11 50 mm VALVE BRIDGE CLAMP", "6061-T6, 12 mm", "Clear anodize + 3 mm EPDM")
    title(c, "P11 50 mm Valve Bridge Clamp")
    cx, cy, s = 150 * mm, 165 * mm, 1.25
    holes = [(-70, -22, 8.5), (70, -22, 8.5), (-70, 22, 8.5), (70, 22, 8.5)]
    slots = [(0, 0, 72, 46)]
    x0, y0, w, h = plan_view(c, cx, cy, 168, 70, s, holes, slots)
    dim_h(c, x0, x0 + w, y0 - 10 * mm, "168")
    dim_v(c, x0 - 10 * mm, y0, y0 + h, "70")
    c.setFont("Helvetica", 8)
    c.drawString(285 * mm, 205 * mm, "Opening: 72 x 46 HOLD")
    c.drawString(285 * mm, 194 * mm, "4 x DIA8.5 at X=+/-70, Y=+/-22")
    c.drawString(285 * mm, 183 * mm, "Use 3 mm EPDM liner")
    notes(c, ["Machine opening only after measuring real valve neck/body landing.", "Clamp the strong valve body/neck; never react torque through thin pipe.", "P12 changes opening to 88 mm for the 63 mm placeholder configuration."])


def drawing_carrier(c: canvas.Canvas, page: int) -> None:
    drawing_title_block(c, page, "P26/P27 BEARING CARRIERS", "6061-T6, 16 mm", "Clear anodize; mask bore", "2")
    title(c, "P26/P27 6204 Bearing Carriers")
    cx, cy, s = 150 * mm, 165 * mm, 2.0
    c.setStrokeColor(DARK); c.circle(cx, cy, 38 * s * mm); c.circle(cx, cy, 23.51 * s * mm)
    for x, y in [(-30, 0), (30, 0), (0, -30), (0, 30)]:
        c.circle(cx + x * s * mm, cy + y * s * mm, 3.3 * s * mm)
    dim_h(c, cx - 38 * s * mm, cx + 38 * s * mm, cy - 47 * s * mm, "DIA76")
    c.setFont("Helvetica", 8)
    c.drawString(275 * mm, 208 * mm, "Bearing bore: DIA47 H7")
    c.drawString(275 * mm, 197 * mm, "Width: 16 +/-0.05")
    c.drawString(275 * mm, 186 * mm, "4 x DIA6.6 on PCD60")
    c.drawString(275 * mm, 175 * mm, "Internal retainers both sides")
    notes(c, ["Finish-machine bearing bore after anodize allowance.", "Use force on bearing outer race only.", "Carriers, not 6 mm plates, provide full 14 mm bearing support."])


def instruction_page(c: canvas.Canvas, page: int) -> None:
    footer(c, page)
    title(c, "Assembly and Calibration Sequence")
    steps = [
        "Measure motor and valve; machine HOLD interfaces only after inspection approval.",
        "Install 6204 bearings in P26/P27 carriers and bolt carriers to P01/P02.",
        "Install P06 shaft, P10 measured adapter, valve clamp, and verify 1-2 mm axial clearance.",
        "Install motor and 40T/80T gears at 90.00 mm centres; tune 0.25-0.35 mm backlash.",
        "Assemble clutch: hub, friction washer, gear, friction washer, pressure plate, Belleville stack, M16 locknut.",
        "Install AS5600, reed cams, hard limits, steel stops, and manual hardware interlock.",
        "Calibrate clutch from 8 N m and set final torque from measured valve data.",
        "Bench test current-limited, then complete ingress, fault, temperature, radio, and endurance tests.",
    ]
    y = PAGE_H - 52 * mm
    for idx, step in enumerate(steps, 1):
        c.setFillColor(GREEN); c.circle(28 * mm, y + 2 * mm, 5 * mm, fill=1, stroke=0)
        c.setFillColor(colors.white); c.setFont("Helvetica-Bold", 9); c.drawCentredString(28 * mm, y - 1 * mm, str(idx))
        c.setFillColor(DARK); c.setFont("Helvetica", 10); c.drawString(39 * mm, y, step)
        y -= 22 * mm
    c.setFillColor(colors.HexColor("#FDEDEC")); c.roundRect(20 * mm, 28 * mm, PAGE_W - 40 * mm, 36 * mm, 3 * mm, fill=1, stroke=0)
    c.setFillColor(RED); c.setFont("Helvetica-Bold", 11); c.drawString(27 * mm, 53 * mm, "DO NOT CONNECT TO CURRENT TB6612 PULSE FIRMWARE")
    c.setFillColor(DARK); c.setFont("Helvetica", 9)
    c.drawString(27 * mm, 43 * mm, "This actuator requires continuous bidirectional PWM, AS5600 feedback, independent reeds/limits, manual inhibit, movement timeout, and current/stall protection.")
    c.drawString(27 * mm, 34 * mm, "Both hard limits and manual mode must remove hazardous motor drive through hardware, even if the MCU or software fails.")


def release_page(c: canvas.Canvas, page: int) -> None:
    footer(c, page)
    title(c, "Prototype Release Hold Points")
    holds = [
        ("Motor interface", "Body, mount, shaft, D-flat, projection, stall current at 9.0/11.1/12.6 V"),
        ("Valve interface", "Neck/body, stem profile/height, axial float, breakaway and running torque wet/dry/hot/cold"),
        ("Power train", "Gear contact, backlash, key/pin/flange, clutch wet/dry torque, stop impact load"),
        ("Electronics", "Driver/BMS/fuse/wire ratings, surge/ESD/reverse polarity, brownout and thermal behavior"),
        ("Functional safety", "Two hard limits, manual inhibit, AS5600 faults, no motion, reversed motion, timeout, radio loss"),
        ("Environment", "IP65/IP67, condensation, UV, corrosion, vibration, temperature, EMC and LoRa coexistence"),
        ("Durability", "500 engineering cycles and 5,000 production-intent pilot cycles"),
        ("Commercial", "Risk assessment, labels, traceability, instructions, maintenance, warranty, Tunisian compliance review"),
    ]
    data = [["Status", "Hold point", "Evidence required"]] + [["OPEN", a, b] for a, b in holds]
    table = Table(data, colWidths=[25 * mm, 68 * mm, PAGE_W - 133 * mm], rowHeights=[12 * mm] + [22 * mm] * len(holds))
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), GREEN), ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"), ("FONTNAME", (0, 1), (-1, -1), "Helvetica"),
        ("FONTSIZE", (0, 0), (-1, -1), 8), ("GRID", (0, 0), (-1, -1), 0.4, GREY),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"), ("BACKGROUND", (0, 1), (0, -1), colors.HexColor("#FDEDEC")),
        ("TEXTCOLOR", (0, 1), (0, -1), RED), ("FONTNAME", (0, 1), (0, -1), "Helvetica-Bold"),
        ("ROWBACKGROUNDS", (1, 1), (-1, -1), [colors.white, LIGHT]),
    ]))
    table.wrapOn(c, PAGE_W - 40 * mm, 190 * mm)
    table.drawOn(c, 20 * mm, 34 * mm)


def generate_pdf() -> None:
    c = canvas.Canvas(str(PDF_PATH), pagesize=landscape(A3), pageCompression=1)
    c.setTitle("AMR 50 mm Valve Actuator Manufacturing Pack")
    c.setAuthor("AMR / Codex")
    page = 1
    cover_page(c, page); c.showPage(); page += 1
    image_page(c, page, "Resolved Assembly Envelope", RENDERS / "AMR_50mm_Valve_Actuator.png", "Placed 50 mm configuration; purchased electronics are reference keep-out envelopes."); c.showPage(); page += 1
    image_page(c, page, "Exploded Assembly Arrangement", RENDERS / "AMR_50mm_Valve_Actuator_exploded.png", "Exploded view is for component identification; use the written stack order for assembly."); c.showPage(); page += 1
    calculations_page(c, page); c.showPage(); page += 1
    page = bom_pages(c, page)
    drawing_functions = [drawing_lower, drawing_upper, drawing_motor, drawing_shaft, drawing_adapter, drawing_hub, drawing_switch, drawing_as5600, drawing_clamp, drawing_carrier]
    for func in drawing_functions:
        func(c, page); c.showPage(); page += 1
    instruction_page(c, page); c.showPage(); page += 1
    release_page(c, page); c.showPage()
    c.save()


def main() -> None:
    prepare_data()
    generate_pdf()
    print(PDF_PATH)


if __name__ == "__main__":
    main()

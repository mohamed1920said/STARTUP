#!/usr/bin/env python3
"""Generate the AMR ESP32-S3 central gateway KiCad prototype project.

The generator keeps the carrier PCB reproducible without depending on KiCad's
GUI.  It emits a legacy schematic that KiCad upgrades on first open and a
fully routed four-layer .kicad_pcb file with dedicated ground planes.
"""

from __future__ import annotations

import csv
import json
import math
import os
import pathlib
import uuid


ROOT = pathlib.Path(__file__).resolve().parent
PROJECT = "central_gateway"


def uid(name: str) -> str:
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"amr-central-gateway/{name}"))


def q(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


NET_NAMES = [
    "GND", "+5V", "+3V3_PERIPH", "VIN_RAW", "VIN_FUSED", "VIN_PROTECTED",
    "LORA_NSS", "LORA_DIO0", "LORA_RST", "LORA_SCK", "LORA_MOSI", "LORA_MISO",
    "LORA_ANT", "LORA_DIO1", "RAIN_GPIO", "RAIN_RAW", "WIND_GPIO", "WIND_RAW",
    "DHT_GPIO", "DHT_RAW", "I2C_SDA_GPIO", "I2C_SDA_RAW", "I2C_SCL_GPIO",
    "I2C_SCL_RAW", "VANE_ADC", "VANE_RAW", "LDR_ADC", "LDR_RAW", "BATT_ADC",
    "BATT_SENSE_RAW", "SETUP_RESET", "UART_TX", "UART_RX", "PWR_LED_A",
]
NET = {name: index + 1 for index, name in enumerate(NET_NAMES)}


def net_clause(name: str | None) -> str:
    if not name:
        return ""
    return f' (net {NET[name]} "{q(name)}")'


class Board:
    def __init__(self) -> None:
        self.footprints: list[str] = []
        self.segments: list[str] = []
        self.vias: list[str] = []
        self.graphics: list[str] = []
        self.pad_xy: dict[tuple[str, str], tuple[float, float]] = {}

    @staticmethod
    def xy(x: float, y: float) -> str:
        return f"{x:.3f} {y:.3f}"

    @staticmethod
    def rot_point(x: float, y: float, angle: float) -> tuple[float, float]:
        r = math.radians(angle)
        # KiCad's board coordinate system has Y increasing down the page, so a
        # positive footprint angle is clockwise in Cartesian terms.
        return x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r)

    def remember_pad(self, ref: str, number: str, origin: tuple[float, float],
                     local: tuple[float, float], angle: float = 0) -> tuple[float, float]:
        dx, dy = self.rot_point(local[0], local[1], angle)
        point = (origin[0] + dx, origin[1] + dy)
        self.pad_xy[(ref, str(number))] = point
        return point

    def pad(self, ref: str, number: int | str) -> tuple[float, float]:
        return self.pad_xy[(ref, str(number))]

    def add_segment(self, net: str, start: tuple[float, float], end: tuple[float, float],
                    width: float = 0.30, layer: str = "F.Cu", tag: str = "") -> None:
        if abs(start[0] - end[0]) < 0.0005 and abs(start[1] - end[1]) < 0.0005:
            return
        self.segments.append(
            f'  (segment (start {self.xy(*start)}) (end {self.xy(*end)}) '
            f'(width {width:.3f}) (layer "{layer}") (net {NET[net]}) '
            f'(uuid "{uid(f"seg/{net}/{tag}/{len(self.segments)}")}"))'
        )

    def add_via(self, net: str, at: tuple[float, float], size: float = 0.80,
                drill: float = 0.40, tag: str = "") -> None:
        self.vias.append(
            f'  (via (at {self.xy(*at)}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net {NET[net]}) '
            f'(uuid "{uid(f"via/{net}/{tag}/{len(self.vias)}")}"))'
        )

    def route_hvh(self, net: str, start: tuple[float, float], end: tuple[float, float],
                  lane_x: float, width: float = 0.30, tag: str = "") -> None:
        a = (lane_x, start[1])
        b = (lane_x, end[1])
        self.add_segment(net, start, a, width, "F.Cu", tag + "/start")
        self.add_via(net, a, tag=tag + "/a")
        self.add_segment(net, a, b, width, "B.Cu", tag + "/vertical")
        self.add_via(net, b, tag=tag + "/b")
        self.add_segment(net, b, end, width, "F.Cu", tag + "/end")

    def route_devkit_left(self, net: str, start: tuple[float, float], end: tuple[float, float],
                          lane_x: float, width: float = 0.25, tag: str = "",
                          transition_y: float | None = None) -> None:
        """Escape a left DevKit pad through the gap between opposite-row pads."""
        gap_y = start[1] + 1.27
        p1 = (51.0, start[1])
        p2 = (54.0, gap_y)
        p3 = (lane_x, gap_y)
        p4 = (lane_x, end[1] if transition_y is None else transition_y)
        self.route_poly(net, [start, p1, p2, p3], width, "F.Cu", tag + "/escape")
        self.add_via(net, p3, size=0.60, drill=0.30, tag=tag + "/a")
        self.add_segment(net, p3, p4, width, "B.Cu", tag + "/vertical")
        self.add_via(net, p4, size=0.60, drill=0.30, tag=tag + "/b")
        # The destination-side fan-out uses the second internal signal layer.
        # This separates it from the source escapes and vertical lanes in the
        # dense DevKit socket area.
        self.add_segment(net, p4, end, width, "In2.Cu", tag + "/end")
        self.add_via(net, end, size=0.60, drill=0.30, tag=tag + "/pad")

    def route_poly(self, net: str, points: list[tuple[float, float]], width: float = 0.30,
                   layer: str = "F.Cu", tag: str = "") -> None:
        for index in range(len(points) - 1):
            self.add_segment(net, points[index], points[index + 1], width, layer, f"{tag}/{index}")

    def add_via_to_ground(self, pad: tuple[float, float], via: tuple[float, float], tag: str) -> None:
        self.add_segment("GND", pad, via, 0.45, "F.Cu", tag)
        self.add_via("GND", via, size=0.90, drill=0.45, tag=tag)

    @staticmethod
    def property_block(ref: str, value: str, ref_at: tuple[float, float],
                       val_at: tuple[float, float]) -> str:
        return f'''
    (property "Reference" "{q(ref)}" (at {ref_at[0]:.3f} {ref_at[1]:.3f} 0) (layer "F.Fab")
      (uuid "{uid(ref + '/prop/ref')}") (effects (font (size 1 1) (thickness 0.15))))
    (property "Value" "{q(value)}" (at {val_at[0]:.3f} {val_at[1]:.3f} 0) (layer "F.Fab")
      (uuid "{uid(ref + '/prop/value')}") (effects (font (size 0.8 0.8) (thickness 0.12))))'''

    def add_two_pad(self, ref: str, value: str, at: tuple[float, float], nets: tuple[str, str],
                    *, spacing: float = 2.5, angle: float = 0, kind: str = "smd",
                    pad_size: tuple[float, float] = (1.4, 1.6), body: tuple[float, float] = (2.0, 1.3),
                    footprint: str = "AMR_2PAD") -> None:
        p1 = (-spacing / 2, 0.0)
        p2 = (spacing / 2, 0.0)
        self.remember_pad(ref, "1", at, p1, angle)
        self.remember_pad(ref, "2", at, p2, angle)
        pads = []
        for number, local, net in (("1", p1, nets[0]), ("2", p2, nets[1])):
            if kind == "thru_hole":
                shape = "roundrect" if number == "1" else "circle"
                rr = " (roundrect_rratio 0.25)" if number == "1" else ""
                pads.append(
                    f'    (pad "{number}" thru_hole {shape} (at {local[0]:.3f} {local[1]:.3f}) '
                    f'(size {pad_size[0]:.3f} {pad_size[1]:.3f}) (drill 1.0) '
                    f'(layers "*.Cu" "*.Mask"){rr}{net_clause(net)})'
                )
            else:
                pads.append(
                    f'    (pad "{number}" smd roundrect (at {local[0]:.3f} {local[1]:.3f}) '
                    f'(size {pad_size[0]:.3f} {pad_size[1]:.3f}) (layers "F.Cu" "F.Paste" "F.Mask") '
                    f'(roundrect_rratio 0.20){net_clause(net)})'
                )
        bw, bh = body
        self.footprints.append(f'''  (footprint "AMR_Central_{footprint}" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f} {angle:.1f})
    {self.property_block(ref, value, (0, -1.8), (0, 1.8))}
    (fp_rect (start {-bw/2:.3f} {-bh/2:.3f}) (end {bw/2:.3f} {bh/2:.3f})
      (stroke (width 0.15) (type solid)) (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
{os.linesep.join(pads)}
  )''')

    def add_connector(self, ref: str, value: str, at: tuple[float, float], nets: list[str],
                      *, pitch: float = 5.08, angle: float = 0, footprint: str = "TerminalBlock") -> None:
        count = len(nets)
        start = -(count - 1) * pitch / 2
        pads = []
        for index, net in enumerate(nets):
            number = str(index + 1)
            local = (start + index * pitch, 0.0)
            self.remember_pad(ref, number, at, local, angle)
            shape = "roundrect" if index == 0 else "circle"
            rr = " (roundrect_rratio 0.20)" if index == 0 else ""
            pads.append(
                f'    (pad "{number}" thru_hole {shape} (at {local[0]:.3f} 0) (size 3.2 3.2) '
                f'(drill 1.3) (layers "*.Cu" "*.Mask"){rr}{net_clause(net)})'
            )
        length = (count - 1) * pitch + 5.0
        self.footprints.append(f'''  (footprint "AMR_Central_{footprint}_{count}" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f} {angle:.1f})
    {self.property_block(ref, value, (0, -3.4), (0, 3.4))}
    (fp_rect (start {-length/2:.3f} -2.5) (end {length/2:.3f} 2.5)
      (stroke (width 0.20) (type solid)) (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
{os.linesep.join(pads)}
  )''')

    def add_rj11_6p6c(self, ref: str, value: str, at: tuple[float, float],
                      nets: list[str | None]) -> None:
        """Wuerth 615006138421 horizontal six-position modular jack.

        Pad and locating-post coordinates are copied from the manufacturer's
        KiCad library. ``at`` is pad 1; the footprint is rotated so the cable
        opening faces the bottom board edge. A 6P6C jack accepts the weather
        kit's RJ11-style 6P4C plugs while exposing only the four used contacts.
        """
        if len(nets) != 6:
            raise ValueError("An RJ11-compatible modular jack requires six positions")
        pad_locations = [(0.0, 0.0), (2.54, 1.02), (0.0, 2.04),
                         (2.54, 3.06), (0.0, 4.08), (2.54, 5.10)]
        pads = []
        for index, (local, net) in enumerate(zip(pad_locations, nets), start=1):
            number = str(index)
            self.remember_pad(ref, number, at, local, 90.0)
            shape = "rect" if index == 1 else "circle"
            pads.append(
                f'    (pad "{number}" thru_hole {shape} (at {local[0]:.3f} {local[1]:.3f}) '
                f'(size 1.4 1.4) (drill 0.9) (layers "*.Cu" "*.Mask"){net_clause(net)})'
            )
        self.footprints.append(f'''  (footprint "AMR_Central_WR-MJ_615006138421" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f} 90.0) (attr through_hole)
    {self.property_block(ref, value, (4.34, -6.0), (4.34, 11.0))}
    (fp_rect (start -2.16 -3.55) (end 10.84 8.65)
      (stroke (width 0.20) (type solid)) (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}") )
    (fp_text user "RJ11 / 6P6C" (at 4.34 2.55) (layer "F.Fab")
      (uuid "{uid(ref + '/label')}") (effects (font (size 0.9 0.9) (thickness 0.15))))
    (pad "" np_thru_hole circle (at 4.84 -3.45) (size 2.36 2.36) (drill 2.36)
      (layers "*.Cu" "*.Mask"))
    (pad "" np_thru_hole circle (at 4.84 8.55) (size 2.36 2.36) (drill 2.36)
      (layers "*.Cu" "*.Mask"))
{os.linesep.join(pads)}
  )''')

    def add_header(self, ref: str, value: str, at: tuple[float, float], nets: list[str],
                   *, pitch: float = 2.54, angle: float = 0) -> None:
        count = len(nets)
        start = -(count - 1) * pitch / 2
        pads = []
        for index, net in enumerate(nets):
            number = str(index + 1)
            local = (start + index * pitch, 0)
            self.remember_pad(ref, number, at, local, angle)
            shape = "roundrect" if index == 0 else "circle"
            rr = " (roundrect_rratio 0.20)" if index == 0 else ""
            pads.append(
                f'    (pad "{number}" thru_hole {shape} (at {local[0]:.3f} 0) (size 1.8 1.8) '
                f'(drill 1.0) (layers "*.Cu" "*.Mask"){rr}{net_clause(net)})'
            )
        length = (count - 1) * pitch + 2.54
        self.footprints.append(f'''  (footprint "AMR_Central_Header_1x{count}" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f} {angle:.1f})
    {self.property_block(ref, value, (0, -2.0), (0, 2.0))}
    (fp_rect (start {-length/2:.3f} -1.27) (end {length/2:.3f} 1.27)
      (stroke (width 0.15) (type solid)) (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
{os.linesep.join(pads)}
  )''')

    def add_devkit(self, at: tuple[float, float]) -> None:
        ref = "U1"
        # Global pad numbering follows the accepted KiCad footprint proposal:
        # J1 = pads 1..22 top-to-bottom; J3 = pads 44..23 top-to-bottom.
        names = {
            1: "3V3", 2: "3V3", 3: "EN", 4: "GPIO4", 5: "GPIO5", 6: "GPIO6",
            7: "GPIO7", 8: "GPIO15", 9: "GPIO16", 10: "GPIO17", 11: "GPIO18",
            12: "GPIO8", 13: "GPIO3", 14: "GPIO46", 15: "GPIO9", 16: "GPIO10",
            17: "GPIO11", 18: "GPIO12", 19: "GPIO13", 20: "GPIO14", 21: "5V",
            22: "GND", 23: "GND", 24: "GND", 25: "GPIO47", 26: "GPIO48",
            27: "GPIO45", 28: "GPIO0", 29: "GPIO35", 30: "GPIO36", 31: "GPIO37",
            32: "GPIO38", 33: "GPIO39", 34: "GPIO40", 35: "GPIO41", 36: "GPIO42",
            37: "GPIO2", 38: "GPIO1", 39: "GPIO44", 40: "GPIO43", 41: "GND",
            42: "GND", 43: "GND", 44: "GND",
        }
        # Correct the global-number mapping for the J3 header.
        names.update({
            44: "GND", 43: "GPIO43", 42: "GPIO44", 41: "GPIO1", 40: "GPIO2",
            39: "GPIO42", 38: "GPIO41", 37: "GPIO40", 36: "GPIO39", 35: "GPIO38",
            34: "GPIO37", 33: "GPIO36", 32: "GPIO35", 31: "GPIO0", 30: "GPIO45",
            29: "GPIO48", 28: "GPIO47", 27: "GPIO21", 26: "GPIO20", 25: "GPIO19",
            24: "GND", 23: "GND",
        })
        pad_nets: dict[int, str | None] = {
            4: "I2C_SDA_GPIO", 5: "I2C_SCL_GPIO", 6: "RAIN_GPIO", 7: "DHT_GPIO",
            11: "LORA_NSS", 12: "BATT_ADC", 13: "WIND_GPIO", 15: "LDR_ADC",
            16: "VANE_ADC", 17: "LORA_MOSI", 18: "LORA_SCK", 19: "LORA_MISO",
            20: "LORA_RST", 21: "+5V", 22: "GND", 23: "GND", 24: "GND",
            31: "SETUP_RESET", 40: "LORA_DIO0", 42: "UART_RX", 43: "UART_TX",
            44: "GND",
        }
        pads = []
        for number in range(1, 23):
            local = (0.0, (number - 1) * 2.54)
            self.remember_pad(ref, str(number), at, local)
            shape = "roundrect" if number == 1 else "circle"
            rr = " (roundrect_rratio 0.20)" if number == 1 else ""
            pads.append(
                f'    (pad "{number}" thru_hole {shape} (at 0 {(number-1)*2.54:.3f}) (size 1.7 1.7) '
                f'(drill 1.0) (layers "*.Cu" "*.Mask"){rr}{net_clause(pad_nets.get(number))} '
                f'(pinfunction "{names[number]}") (pintype "passive"))'
            )
        for row_index, number in enumerate(range(44, 22, -1)):
            local = (22.86, row_index * 2.54)
            self.remember_pad(ref, str(number), at, local)
            pads.append(
                f'    (pad "{number}" thru_hole circle (at 22.86 {row_index*2.54:.3f}) (size 1.7 1.7) '
                f'(drill 1.0) (layers "*.Cu" "*.Mask"){net_clause(pad_nets.get(number))} '
                f'(pinfunction "{names[number]}") (pintype "passive"))'
            )
        self.footprints.append(f'''  (footprint "AMR_Central_ESP32-S3-DevKitC-1_Carrier" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr through_hole)
    {self.property_block(ref, "ESP32-S3-DevKitC-1-N8R8", (11.43, 56.0), (11.43, 58.0))}
    (fp_rect (start -1.27 -1.27) (end 24.13 61.47)
      (stroke (width 0.25) (type solid)) (fill none) (layer "F.Fab") (uuid "{uid(ref + '/outline')}"))
    (fp_rect (start 2.50 -7.30) (end 20.50 18.20)
      (stroke (width 0.18) (type solid)) (fill none) (layer "F.Fab") (uuid "{uid(ref + '/module')}"))
    (fp_rect (start -13.10 -23.30) (end 36.00 -1.30)
      (stroke (width 0.15) (type dash)) (fill none) (layer "Dwgs.User") (uuid "{uid(ref + '/keepout-mark')}"))
    (fp_text user "WIFI ANTENNA - NO CARRIER PCB" (at 11.43 -4.5) (layer "F.Fab")
      (uuid "{uid(ref + '/antenna-text')}") (effects (font (size 0.9 0.9) (thickness 0.15))))
    (fp_text user "USB / PROGRAM" (at 11.43 60.0) (layer "F.Fab")
      (uuid "{uid(ref + '/usb-text')}") (effects (font (size 0.9 0.9) (thickness 0.15))))
{os.linesep.join(pads)}
  )''')

    def add_rfm95(self, at: tuple[float, float]) -> None:
        ref = "U2"
        names = {1:"GND",2:"MISO",3:"MOSI",4:"SCK",5:"NSS",6:"RESET",7:"DIO5",8:"GND",
                 9:"ANT",10:"GND",11:"DIO3",12:"DIO4",13:"3V3",14:"DIO0",15:"DIO1",16:"DIO2"}
        nets = {1:"GND",2:"LORA_MISO",3:"LORA_MOSI",4:"LORA_SCK",5:"LORA_NSS",6:"LORA_RST",
                8:"GND",9:"LORA_ANT",10:"GND",13:"+3V3_PERIPH",14:"LORA_DIO0",15:"LORA_DIO1"}
        pads = []
        for number in range(1, 9):
            local = (-7.525, -7 + (number - 1) * 2)
            self.remember_pad(ref, number, at, local)
            pads.append(
                f'    (pad "{number}" smd roundrect (at -7.525 {local[1]:.3f}) (size 2.95 1.27) '
                f'(layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.20){net_clause(nets.get(number))} '
                f'(pinfunction "{names[number]}") (pintype "passive"))'
            )
        for row_index, number in enumerate(range(16, 8, -1)):
            local = (7.525, -7 + row_index * 2)
            self.remember_pad(ref, number, at, local)
            pads.append(
                f'    (pad "{number}" smd roundrect (at 7.525 {local[1]:.3f}) (size 2.95 1.27) '
                f'(layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.20){net_clause(nets.get(number))} '
                f'(pinfunction "{names[number]}") (pintype "passive"))'
            )
        self.footprints.append(f'''  (footprint "AMR_Central_HOPERF_RFM95W_SMD" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr smd)
    {self.property_block(ref, "RFM95W-868S2", (0, -9.5), (0, 9.5))}
    (fp_rect (start -8 -8) (end 8 8) (stroke (width 0.20) (type solid))
      (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
    (fp_text user "RFM95W 868 MHz" (at 0 0) (layer "F.Fab")
      (uuid "{uid(ref + '/label')}") (effects (font (size 1 1) (thickness 0.15))))
{os.linesep.join(pads)}
  )''')

    def add_dcdc(self, at: tuple[float, float]) -> None:
        ref = "U3"
        pins = [("1", (2.54, 0), "VIN_PROTECTED", "VIN"), ("2", (0, 0), "GND", "GND"),
                ("3", (-2.54, 0), "+5V", "VOUT")]
        pads = []
        for number, local, net, function in pins:
            self.remember_pad(ref, number, at, local)
            shape = "roundrect" if number == "1" else "circle"
            rr = " (roundrect_rratio 0.20)" if number == "1" else ""
            pads.append(
                f'    (pad "{number}" thru_hole {shape} (at {local[0]:.3f} 0) (size 2.2 2.2) '
                f'(drill 1.0) (layers "*.Cu" "*.Mask"){rr}{net_clause(net)} '
                f'(pinfunction "{function}") (pintype "passive"))'
            )
        self.footprints.append(f'''  (footprint "AMR_Central_RECOM_R-78E5.0-1.0_SIP3" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr through_hole)
    {self.property_block(ref, "R-78E5.0-1.0", (0, -4.0), (0, 4.0))}
    (fp_rect (start -5.8 -3.4) (end 5.8 3.4) (stroke (width 0.20) (type solid))
      (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
{os.linesep.join(pads)}
  )''')

    def add_ldo(self, at: tuple[float, float]) -> None:
        ref = "U4"
        pins = {
            "1": ((-1.2, -0.95), "+5V", "VIN"), "2": ((-1.2, 0), "GND", "GND"),
            "3": ((-1.2, 0.95), "+5V", "EN"), "4": ((1.2, 0.95), None, "NC"),
            "5": ((1.2, -0.95), "+3V3_PERIPH", "VOUT"),
        }
        pads = []
        for number, (local, net, function) in pins.items():
            self.remember_pad(ref, number, at, local)
            pads.append(
                f'    (pad "{number}" smd roundrect (at {local[0]:.3f} {local[1]:.3f}) (size 1.4 0.65) '
                f'(layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.20){net_clause(net)} '
                f'(pinfunction "{function}") (pintype "passive"))'
            )
        self.footprints.append(f'''  (footprint "AMR_Central_SOT-23-5" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr smd)
    {self.property_block(ref, "AP2112K-3.3", (0, -2.4), (0, 2.4))}
    (fp_rect (start -1.55 -1.55) (end 1.55 1.55) (stroke (width 0.15) (type solid))
      (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
{os.linesep.join(pads)}
  )''')

    def add_sma(self, at: tuple[float, float]) -> None:
        ref = "J9"
        padspec = [("1", (0, 0), "LORA_ANT"), ("2", (-2.54, -2.54), "GND"),
                   ("3", (2.54, -2.54), "GND"), ("4", (-2.54, 2.54), "GND"),
                   ("5", (2.54, 2.54), "GND")]
        pads = []
        for number, local, net in padspec:
            self.remember_pad(ref, number, at, local)
            pads.append(
                f'    (pad "{number}" thru_hole circle (at {local[0]:.3f} {local[1]:.3f}) '
                f'(size {2.2 if number == "1" else 2.8:.1f} {2.2 if number == "1" else 2.8:.1f}) '
                f'(drill {1.1 if number == "1" else 1.4:.1f}) (layers "*.Cu" "*.Mask"){net_clause(net)})'
            )
        self.footprints.append(f'''  (footprint "AMR_Central_SMA_Vertical_THT" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr through_hole)
    {self.property_block(ref, "SMA_868MHz", (0, -5.0), (0, 5.0))}
    (fp_circle (center 0 0) (end 3.8 0) (stroke (width 0.20) (type solid))
      (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
{os.linesep.join(pads)}
  )''')

    def add_panel_button_connector(self, at: tuple[float, float]) -> None:
        """Two-pin locking header for the enclosure-mounted setup/reset button."""
        ref = "J10"
        for number, local, net in (("1", (-1.25, 0.0), "SETUP_RESET"),
                                   ("2", (1.25, 0.0), "GND")):
            self.remember_pad(ref, number, at, local)
        self.footprints.append(f'''  (footprint "AMR_Central_JST_XH_B2B-XH-A_1x02_P2.50mm" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr through_hole)
    {self.property_block(ref, "PANEL SETUP BUTTON", (0, -4.3), (0, 3.3))}
    (fp_rect (start -3.7 -3.4) (end 3.7 2.3) (stroke (width 0.20) (type solid))
      (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
    (fp_text user "PANEL SW" (at 0 -2.3) (layer "F.SilkS")
      (uuid "{uid(ref + '/label')}") (effects (font (size 0.8 0.8) (thickness 0.12))))
    (pad "1" thru_hole roundrect (at -1.25 0) (size 1.8 1.8) (drill 1.0)
      (layers "*.Cu" "*.Mask") (roundrect_rratio 0.20){net_clause("SETUP_RESET")})
    (pad "2" thru_hole circle (at 1.25 0) (size 1.8 1.8) (drill 1.0)
      (layers "*.Cu" "*.Mask"){net_clause("GND")})
  )''')

    def add_testpoint(self, ref: str, value: str, at: tuple[float, float], net: str) -> None:
        self.remember_pad(ref, "1", at, (0, 0))
        self.footprints.append(f'''  (footprint "AMR_Central_TestPoint_THT" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr through_hole)
    {self.property_block(ref, value, (0, -2.0), (0, 2.0))}
    (pad "1" thru_hole circle (at 0 0) (size 2.2 2.2) (drill 1.0)
      (layers "*.Cu" "*.Mask"){net_clause(net)})
  )''')

    def add_mounting_hole(self, ref: str, at: tuple[float, float]) -> None:
        self.footprints.append(f'''  (footprint "AMR_Central_MountingHole_3.2mm" (layer "F.Cu")
    (uuid "{uid(ref + '/fp')}") (at {at[0]:.3f} {at[1]:.3f}) (attr exclude_from_bom exclude_from_pos_files)
    {self.property_block(ref, "M3", (0, -4.0), (0, 4.0))}
    (fp_circle (center 0 0) (end 4 0) (stroke (width 0.20) (type solid))
      (fill none) (layer "F.Fab") (uuid "{uid(ref + '/body')}"))
    (pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask"))
  )''')


def build_board() -> str:
    b = Board()
    b.add_devkit((40.0, 34.2))
    b.add_rfm95((102.0, 49.0))
    b.add_dcdc((120.0, 31.0))
    b.add_ldo((120.0, 42.0))

    # Power input and protection.
    b.add_connector("J1", "POWER 7-18V DC", (141.5, 31.0), ["VIN_RAW", "GND"])
    b.add_two_pad("F1", "PTC 0.75A", (135.0, 31.0), ("VIN_FUSED", "VIN_RAW"),
                  spacing=3.0, kind="smd", pad_size=(2.0, 2.8), body=(3.8, 2.6), footprint="Fuse_1812")
    b.add_two_pad("D1", "SMBJ18A", (133.5, 35.5), ("VIN_FUSED", "GND"),
                  spacing=3.2, angle=90, pad_size=(2.4, 2.2), body=(4.2, 2.2), footprint="D_SMB")
    b.add_two_pad("D2", "SS34", (129.0, 31.0), ("VIN_PROTECTED", "VIN_FUSED"),
                  spacing=3.0, pad_size=(2.0, 2.2), body=(3.8, 2.1), footprint="D_SMA")
    b.add_two_pad("C1", "10uF 35V", (125.0, 35.0), ("GND", "VIN_PROTECTED"), angle=90,
                  spacing=3.2, pad_size=(1.8, 2.6), body=(3.2, 2.5), footprint="C_1210")
    b.add_two_pad("C2", "22uF 10V", (116.0, 35.0), ("GND", "+5V"), angle=90,
                  spacing=3.2, pad_size=(1.8, 2.2), body=(3.2, 1.8), footprint="C_1206")
    b.add_two_pad("R1", "1k", (110.0, 31.0), ("PWR_LED_A", "+5V"), footprint="R_0805")
    b.add_two_pad("LED1", "GREEN", (106.0, 31.0), ("GND", "PWR_LED_A"), footprint="LED_0805")
    b.add_two_pad("C3", "1uF", (115.0, 42.0), ("GND", "+5V"), angle=90, footprint="C_0805")
    b.add_two_pad("C4", "1uF", (125.0, 42.0), ("GND", "+3V3_PERIPH"), angle=90, footprint="C_0805")

    # LoRa RF, local decoupling, antenna and optional DIO1 test point.
    b.add_two_pad("C5", "10uF", (114.0, 50.0), ("GND", "+3V3_PERIPH"), angle=90, footprint="C_0805")
    b.add_two_pad("C6", "100nF", (116.0, 52.0), ("GND", "+3V3_PERIPH"), angle=90, footprint="C_0805")
    b.add_sma((145.5, 56.0))
    b.add_testpoint("TP4", "LORA_DIO1", (113.0, 44.0), "LORA_DIO1")

    # Field connectors. The six-position jacks accept the kit's RJ11-style
    # 6P4C plugs. Rain uses the center pair. The combined wind cable uses the
    # four populated contacts: vane on the outer pair and speed on the center.
    b.add_rj11_6p6c("J2", "RAIN RJ11", (72.45, 97.84),
                    [None, None, "GND", "RAIN_RAW", None, None])
    b.add_rj11_6p6c("J3", "WIND+VANE RJ11", (88.45, 97.84),
                    [None, "VANE_RAW", "GND", "WIND_RAW", "GND", None])
    b.add_connector("J4", "DHT11", (107.5, 95.0), ["+3V3_PERIPH", "DHT_RAW", "GND"])
    b.add_connector("J5", "BMP280 I2C", (134.0, 95.0), ["+3V3_PERIPH", "I2C_SDA_RAW", "I2C_SCL_RAW", "GND"])
    b.add_connector("J6", "PASSIVE LDR", (145.0, 75.0),
                    ["LDR_RAW", "GND"], angle=90)
    b.add_connector("J7", "BAT SENSE 0-6V", (136.0, 51.0), ["BATT_SENSE_RAW", "GND"])
    b.add_header("J8", "SERVICE UART", (84.0, 30.5), ["+3V3_PERIPH", "GND", "UART_TX", "UART_RX"])
    b.add_panel_button_connector((82.0, 58.0))

    # Rain input conditioning.
    b.add_two_pad("R2", "1k", (72.71, 80.00), ("RAIN_GPIO", "RAIN_RAW"), footprint="R_0805")
    b.add_two_pad("R3", "10k", (76.0, 82.5), ("RAIN_GPIO", "+3V3_PERIPH"), angle=90, footprint="R_0805")
    b.add_two_pad("D3", "PESD3V3", (71.0, 84.00), ("RAIN_RAW", "GND"), angle=90, footprint="D_SOD323")

    # Wind input conditioning.
    b.add_two_pad("R4", "1k", (83.71, 77.50), ("WIND_GPIO", "WIND_RAW"), footprint="R_0805")
    b.add_two_pad("R5", "10k", (80.0, 82.0), ("WIND_GPIO", "+3V3_PERIPH"), angle=90, footprint="R_0805")
    b.add_two_pad("D4", "PESD3V3", (99.0, 82.00), ("WIND_RAW", "GND"), angle=90, footprint="D_SOD323")

    # DHT digital input.
    b.add_two_pad("R6", "220R", (99.75, 75.00), ("DHT_GPIO", "DHT_RAW"), footprint="R_0805")
    b.add_two_pad("R7", "4.7k", (95.0, 76.25), ("+3V3_PERIPH", "DHT_GPIO"), angle=90, footprint="R_0805")
    b.add_two_pad("D5", "PESD3V3", (104.0, 86.50), ("DHT_RAW", "GND"), angle=90, footprint="D_SOD323")

    # I2C filtering, pull-ups and ESD.
    b.add_two_pad("R8", "100R", (115.71, 70.00), ("I2C_SDA_GPIO", "I2C_SDA_RAW"), footprint="R_0805")
    b.add_two_pad("R9", "100R", (120.79, 72.50), ("I2C_SCL_GPIO", "I2C_SCL_RAW"), footprint="R_0805")
    b.add_two_pad("R10", "4.7k", (110.0, 68.75), ("I2C_SDA_GPIO", "+3V3_PERIPH"), angle=90, footprint="R_0805")
    b.add_two_pad("R11", "4.7k", (126.0, 73.75), ("+3V3_PERIPH", "I2C_SCL_GPIO"), angle=90, footprint="R_0805")
    b.add_two_pad("D6", "PESD3V3", (113.5, 86.50), ("I2C_SDA_RAW", "GND"), angle=90, footprint="D_SOD323")
    b.add_two_pad("D7", "PESD3V3", (125.5, 86.50), ("I2C_SCL_RAW", "GND"), angle=90, footprint="D_SOD323")

    # Analog vane and passive LDR inputs.
    b.add_two_pad("R12", "1k", (132.0, 64.00), ("VANE_ADC", "VANE_RAW"), footprint="R_0805")
    b.add_two_pad("R13", "10k", (126.0, 64.00), ("VANE_ADC", "+3V3_PERIPH"), footprint="R_0805")
    b.add_two_pad("D8", "PESD3V3", (141.0, 67.00), ("VANE_RAW", "GND"), angle=90, footprint="D_SOD323")
    b.add_two_pad("R14", "1k", (132.0, 66.50), ("LDR_ADC", "LDR_RAW"), footprint="R_0805")
    b.add_two_pad("R15", "10k", (126.0, 66.50), ("LDR_ADC", "+3V3_PERIPH"), footprint="R_0805")
    b.add_two_pad("D9", "PESD3V3", (138.0, 74.00), ("LDR_RAW", "GND"), angle=90, footprint="D_SOD323")

    # Battery monitor; divider exactly matches WeatherStation.cpp (2:1).
    b.add_two_pad("R16", "100k 1%", (126.0, 59.46), ("BATT_ADC", "BATT_SENSE_RAW"), footprint="R_0805")
    b.add_two_pad("R17", "100k 1%", (122.0, 60.71), ("GND", "BATT_ADC"), angle=90, footprint="R_0805")
    b.add_two_pad("C11", "100nF", (119.0, 60.71), ("GND", "BATT_ADC"), angle=90, footprint="C_0805")
    b.add_two_pad("D10", "BZT52C6V2", (131.0, 60.71), ("BATT_SENSE_RAW", "GND"), angle=90, footprint="D_SOD323")

    # Test points and mounting holes.
    b.add_testpoint("TP1", "+5V", (58.0, 92.0), "+5V")
    b.add_testpoint("TP2", "+3V3", (63.0, 96.0), "+3V3_PERIPH")
    b.add_testpoint("TP3", "GND", (52.0, 92.0), "GND")
    for ref, point in (("H1", (24.0, 29.5)), ("H2", (146.0, 41.0)),
                       ("H3", (24.0, 99.0)), ("H4", (146.0, 99.0))):
        b.add_mounting_hole(ref, point)

    # Primary firmware-to-radio routes.
    for net, source_pad, target_pad, lane, transition_y in (
        ("LORA_NSS", 11, 5, 64.2, 49.2), ("LORA_MOSI", 17, 3, 65.0, 46.5),
        ("LORA_SCK", 18, 4, 65.8, 47.3), ("LORA_MISO", 19, 2, 66.6, 44.5),
        ("LORA_RST", 20, 6, 67.4, 52.0),
    ):
        b.route_devkit_left(net, b.pad("U1", source_pad), b.pad("U2", target_pad), lane,
                            tag=net, transition_y=transition_y)
    # GPIO2/DIO0 runs below the ground plane route layer and enters the SMD pad from inside.
    dio0_start = b.pad("U1", 40)
    dio0_end = b.pad("U2", 14)
    b.route_poly("LORA_DIO0", [dio0_start, (70.0, 38.0), (106.0, 38.0), (106.0, 46.0)],
                 0.30, "In2.Cu", "dio0-inner")
    b.add_via("LORA_DIO0", (106.0, 46.0), tag="dio0-entry")
    b.add_segment("LORA_DIO0", (106.0, 46.0), dio0_end, 0.30, "F.Cu", "dio0-pad")

    # Weather signal routes from the exact GPIO pads to the protected interface circuits.
    signal_routes = [
        ("I2C_SDA_GPIO", 4, "R8", 1, 69.0), ("I2C_SCL_GPIO", 5, "R9", 1, 69.8),
        ("RAIN_GPIO", 6, "R2", 1, 70.6), ("DHT_GPIO", 7, "R6", 1, 71.4),
        ("BATT_ADC", 12, "R16", 1, 72.2), ("WIND_GPIO", 13, "R4", 1, 73.0),
        ("LDR_ADC", 15, "R15", 1, 73.8), ("VANE_ADC", 16, "R13", 1, 74.6),
    ]
    for net, source_pad, target_ref, target_pad, lane in signal_routes:
        b.route_devkit_left(net, b.pad("U1", source_pad), b.pad(target_ref, target_pad), lane, tag=net)

    # Enclosure-mounted setup/reset button and UART header.
    b.route_poly("SETUP_RESET", [b.pad("U1", 31), (66.0, 69.0), (78.0, 69.0),
                                  (78.0, b.pad("J10", 1)[1]), b.pad("J10", 1)],
                 0.35, "F.Cu", "setup")
    b.add_segment("GND", b.pad("J10", 2), (87.0, 58.0),
                  0.45, "F.Cu", "setup-ground")
    b.add_via("GND", (87.0, 58.0), size=0.90, drill=0.45,
              tag="setup-ground")
    for net, source, target, tag in (
        ("UART_TX", b.pad("U1", 43), b.pad("J8", 3), "uart-tx"),
        ("UART_RX", b.pad("U1", 42), b.pad("J8", 4), "uart-rx"),
    ):
        corner = (target[0], source[1])
        b.add_segment(net, source, corner, 0.30, "F.Cu", tag + "-top")
        b.add_via(net, corner, tag=tag + "-via")
        b.add_segment(net, corner, target, 0.30, "B.Cu", tag + "-bottom")

    # Antenna feed: short and straight. Final width must be impedance-tuned to
    # the selected fabricator's four-layer stack-up before production.
    b.add_segment("LORA_ANT", b.pad("U2", 9), b.pad("J9", 1), 0.45, "F.Cu", "rf-feed")
    b.add_segment("LORA_DIO1", b.pad("U2", 15), b.pad("TP4", 1), 0.30, "F.Cu", "dio1-tp")

    # Power route and local supply distribution.
    direct_routes = [
        ("VIN_RAW", "J1", 1, "F1", 2, 0.80),
        ("VIN_FUSED", "F1", 1, "D2", 2, 0.80),
        ("VIN_PROTECTED", "D2", 1, "U3", 1, 0.80),
        ("VIN_PROTECTED", "D2", 1, "C1", 2, 0.60),
        ("+5V", "U3", 3, "C2", 2, 0.80),
        ("+5V", "U3", 3, "R1", 2, 0.50),
        ("PWR_LED_A", "R1", 1, "LED1", 2, 0.35),
        ("+5V", "U4", 1, "C3", 2, 0.50),
        ("+3V3_PERIPH", "U4", 5, "C4", 2, 0.60),
    ]
    for net, ra, pa, rb, pb, width in direct_routes:
        b.add_segment(net, b.pad(ra, pa), b.pad(rb, pb), width, "F.Cu", f"{ra}-{rb}")
    # Approach the TVS cathode from the side so the fused-input trace does not
    # pass through the adjacent grounded anode pad.
    b.route_poly("VIN_FUSED", [b.pad("F1", 1), (130.5, b.pad("F1", 1)[1]),
                                (130.5, b.pad("D1", 1)[1]), b.pad("D1", 1)],
                 0.60, "F.Cu", "F1-D1-tvs")
    b.route_poly("+5V", [b.pad("U3", 3), (112.5, 31.0), (112.5, 39.5), b.pad("U4", 1)],
                 0.60, "F.Cu", "5v-ldo")
    b.route_poly("+5V", [b.pad("U4", 1), (117.0, b.pad("U4", 1)[1]),
                          (117.0, b.pad("U4", 3)[1]), b.pad("U4", 3)],
                 0.50, "F.Cu", "ldo-enable")
    b.route_poly("+5V", [b.pad("U3", 3), (117.3, 31.0), (117.3, 49.0),
                          (112.0, 49.0), (112.0, 60.0),
                          (108.8, 60.0), (108.8, 92.0),
                          (117.0, 92.0), (117.0, 101.5), (35.0, 101.5),
                          (35.0, b.pad("U1", 21)[1])],
                 0.90, "B.Cu", "5v-devkit-bottom")
    b.add_via("+5V", (35.0, b.pad("U1", 21)[1]), size=1.2, drill=0.6, tag="5v-devkit-entry")
    b.add_segment("+5V", (35.0, b.pad("U1", 21)[1]), b.pad("U1", 21),
                  0.90, "F.Cu", "5v-devkit-pad")
    b.route_poly("+5V", [b.pad("U1", 21), (55.0, b.pad("U1", 21)[1]),
                          (55.0, b.pad("TP1", 1)[1]), b.pad("TP1", 1)],
                 0.60, "F.Cu", "5v-tp")

    # 3V3 peripheral trunk: top via, bottom-layer vertical/horizontal bus, then short branches.
    rail_origin = b.pad("U4", 5)
    rail_top = (129.5, rail_origin[1])
    rail_bottom = (129.5, 89.8)
    b.add_segment("+3V3_PERIPH", rail_origin, rail_top, 0.65, "F.Cu", "3v3-origin")
    b.add_via("+3V3_PERIPH", rail_top, size=1.0, drill=0.5, tag="3v3-top")
    b.add_segment("+3V3_PERIPH", rail_top, rail_bottom, 0.65, "B.Cu", "3v3-vertical")
    b.add_via("+3V3_PERIPH", rail_bottom, size=1.0, drill=0.5, tag="3v3-bottom")
    # RFM supply and decoupling from the top rail.
    b.route_poly("+3V3_PERIPH", [rail_top, (129.5, 48.0), (112.0, 48.0),
                                  b.pad("U2", 13)], 0.60, "F.Cu", "3v3-rfm")
    b.add_segment("+3V3_PERIPH", b.pad("U2", 13), b.pad("C5", 2), 0.50, "F.Cu", "3v3-C5")
    b.add_segment("+3V3_PERIPH", b.pad("C5", 2), b.pad("C6", 2), 0.50, "F.Cu", "3v3-C6")
    # Bus and branches to connectors, pull-ups and test points.
    b.add_segment("+3V3_PERIPH", (76.0, 89.8), (129.5, 89.8), 0.65, "F.Cu", "3v3-bus")
    b.route_poly("+3V3_PERIPH", [b.pad("TP2", 1), (66.0, 96.0),
                                   (66.0, 88.5), (76.0, 88.5), (76.0, 89.8)],
                 0.45, "F.Cu", "3v3-TP2-branch")
    for ref, padno in (("J4", 1), ("J5", 1),
                       ("R7", 1),
                       ):
        point = b.pad(ref, padno)
        via = (point[0], 89.8)
        if abs(point[1] - 89.8) < 0.1 and ref.startswith("J"):
            continue
        b.add_segment("+3V3_PERIPH", point, via, 0.45, "F.Cu", f"3v3-{ref}-branch")
    # These two vertical pull-ups have their signal pad toward the lower bus;
    # escape through In2.Cu so the supply does not run through that signal pad.
    for ref in ("R3", "R5"):
        point = b.pad(ref, 2)
        bus_point = (77.5 if ref == "R3" else point[0], 89.8)
        b.add_via("+3V3_PERIPH", point, size=0.60, drill=0.30, tag=f"3v3-{ref}-pad")
        b.add_segment("+3V3_PERIPH", point, bus_point, 0.45,
                      "B.Cu" if ref == "R3" else "In2.Cu", f"3v3-{ref}-inner")
        b.add_via("+3V3_PERIPH", bus_point, size=0.60, drill=0.30, tag=f"3v3-{ref}-bus")
    for ref in ("R13", "R15"):
        point = b.pad(ref, 2)
        b.add_via("+3V3_PERIPH", point, size=0.60, drill=0.30, tag=f"3v3-{ref}")
        b.route_poly("+3V3_PERIPH", [point, (129.5, point[1]), (129.5, 89.8)],
                     0.45, "B.Cu", f"3v3-{ref}-rail")
    r10_power = b.pad("R10", 2)
    b.add_via("+3V3_PERIPH", r10_power, size=0.60, drill=0.30, tag="3v3-R10")
    b.add_segment("+3V3_PERIPH", r10_power, (r10_power[0], 89.8),
                  0.45, "B.Cu", "3v3-R10-rail")
    b.add_via("+3V3_PERIPH", (r10_power[0], 89.8), tag="3v3-R10-bus-via")
    r11_power = b.pad("R11", 1)
    b.add_via("+3V3_PERIPH", r11_power, size=0.60, drill=0.30, tag="3v3-R11")
    b.add_segment("+3V3_PERIPH", r11_power, (129.5, r11_power[1]), 0.45, "In2.Cu", "3v3-R11-rail")
    b.add_via("+3V3_PERIPH", (129.5, r11_power[1]), tag="3v3-R11-rail-via")
    b.route_poly("+3V3_PERIPH", [b.pad("J8", 1), (b.pad("J8", 1)[0], 33.5),
                                  (129.5, 33.5), rail_top],
                 0.45, "In2.Cu", "3v3-uart-inner")

    # Local protected-interface routes.
    # Local interface trees. Components are aligned so signal trunks never cross an opposite-net pad.
    def straight(net: str, ra: str, pa: int, rb: str, pb: int, tag: str) -> None:
        b.add_segment(net, b.pad(ra, pa), b.pad(rb, pb), 0.30, "F.Cu", tag)

    def raw_to_connector(net: str, series: str, connector: str, pin: int, tag: str,
                         waypoints: list[tuple[float, float]] | None = None,
                         layer: str = "B.Cu") -> None:
        source, target = b.pad(series, 2), b.pad(connector, pin)
        b.add_via(net, source, size=0.60, drill=0.30, tag=tag + "-via")
        points = [source] + (waypoints if waypoints is not None else
                             [(target[0], source[1])]) + [target]
        b.route_poly(net, points, 0.30, layer, tag + "-route")

    for prefix, series, pullup, esd, connector in (
        ("RAIN", "R2", "R3", "D3", "J2"),
        ("WIND", "R4", "R5", "D4", "J3"),
    ):
        gpio, raw = f"{prefix}_GPIO", f"{prefix}_RAW"
        if prefix == "WIND":
            source, target = b.pad(pullup, 1), b.pad(series, 1)
            b.route_poly(gpio, [source, (82.0, source[1]), (82.0, 80.0), target],
                         0.30, "F.Cu", prefix + "-gpio")
        else:
            straight(gpio, pullup, 1, series, 1, prefix + "-gpio")
        target = b.pad(connector, 4)
        waypoints = ([(78.0, b.pad(series, 2)[1]), (78.0, 88.0),
                      (target[0], 88.0)]
                     if prefix == "RAIN" else
                     [(86.5, b.pad(series, 2)[1]), (86.5, 80.0),
                      (target[0], 80.0)])
        raw_to_connector(raw, series, connector, 4, prefix + "-raw-main", waypoints,
                         layer="In2.Cu" if prefix == "RAIN" else "B.Cu")
        if prefix == "RAIN":
            b.route_poly(raw, [b.pad(esd, 1), (69.0, b.pad(esd, 1)[1]),
                               (69.0, 76.0), (b.pad(series, 2)[0], 76.0),
                               b.pad(series, 2)], 0.30, "F.Cu", prefix + "-esd")
        else:
            b.add_via(raw, b.pad(esd, 1), size=0.60, drill=0.30, tag=prefix + "-esd-via")
            b.route_poly(raw, [b.pad(esd, 1), (86.5, b.pad(esd, 1)[1]),
                               (86.5, b.pad(series, 2)[1]), b.pad(series, 2)],
                         0.30, "B.Cu", prefix + "-esd")

    straight("DHT_GPIO", "R7", 2, "R6", 1, "dht-gpio")
    raw_to_connector("DHT_RAW", "R6", "J4", 2, "dht-raw",
                     [(107.0, b.pad("R6", 2)[1]), (107.0, b.pad("J4", 2)[1])])
    b.add_via("DHT_RAW", b.pad("D5", 1), size=0.60, drill=0.30, tag="dht-esd-via")
    b.add_segment("DHT_RAW", b.pad("D5", 1),
                  (107.0, b.pad("D5", 1)[1]), 0.30, "B.Cu", "dht-esd")

    straight("I2C_SDA_GPIO", "R10", 1, "R8", 1, "sda-gpio")
    sda_target = b.pad("J5", 2)
    sda_source = b.pad("R8", 2)
    b.add_via("I2C_SDA_RAW", sda_source, size=0.60, drill=0.30, tag="sda-raw-via")
    b.route_poly("I2C_SDA_RAW", [sda_source, (sda_source[0], 91.0), (128.0, 91.0)],
                 0.30, "B.Cu", "sda-raw-bottom")
    b.add_via("I2C_SDA_RAW", (128.0, 91.0), size=0.60, drill=0.30, tag="sda-layer-change")
    b.route_poly("I2C_SDA_RAW", [(128.0, 91.0), (sda_target[0], 91.0), sda_target],
                 0.30, "In2.Cu", "sda-raw-inner")
    b.add_via("I2C_SDA_RAW", b.pad("D6", 1), size=0.60, drill=0.30, tag="sda-esd-via")
    b.add_segment("I2C_SDA_RAW", b.pad("D6", 1), (b.pad("R8", 2)[0], b.pad("D6", 1)[1]),
                  0.30, "B.Cu", "sda-esd")
    b.route_poly("I2C_SCL_GPIO", [b.pad("R9", 1), (b.pad("R9", 1)[0], 69.0),
                                   (b.pad("R11", 2)[0], 69.0), b.pad("R11", 2)],
                 0.30, "F.Cu", "scl-gpio")
    scl_source, scl_target = b.pad("R9", 2), b.pad("J5", 3)
    b.add_via("I2C_SCL_RAW", scl_source, size=0.60, drill=0.30, tag="scl-raw-via")
    b.route_poly("I2C_SCL_RAW", [scl_source, (scl_source[0], 77.0), (132.0, 77.0)],
                 0.30, "In2.Cu", "scl-raw-inner")
    b.add_via("I2C_SCL_RAW", (132.0, 77.0), size=0.60, drill=0.30, tag="scl-layer-change")
    b.route_poly("I2C_SCL_RAW", [(132.0, 77.0), (132.0, 80.0), (140.5, 80.0),
                                   (140.5, 90.0), (scl_target[0], 90.0), scl_target],
                 0.30, "B.Cu", "scl-raw-bottom")
    b.add_via("I2C_SCL_RAW", b.pad("D7", 1), size=0.60, drill=0.30, tag="scl-esd-via")
    b.route_poly("I2C_SCL_RAW", [b.pad("R9", 2),
                                   (b.pad("R9", 2)[0], b.pad("D7", 1)[1]),
                                   b.pad("D7", 1)], 0.30, "B.Cu", "scl-esd")

    for net, a, z, detour in (("VANE_ADC", b.pad("R13", 1), b.pad("R12", 1), 1.25),
                              ("LDR_ADC", b.pad("R15", 1), b.pad("R14", 1), 1.5)):
        b.add_via(net, z, size=0.60, drill=0.30, tag=net + "-pullup-z")
        b.route_poly(net, [a, (a[0], a[1] + detour), (z[0], z[1] + detour), z],
                     0.30, "In2.Cu", net + "-pullup")
    b.add_via("VANE_RAW", b.pad("R12", 2), size=0.60, drill=0.30, tag="vane-raw-a")
    b.route_poly("VANE_RAW", [b.pad("R12", 2), (139.0, b.pad("R12", 2)[1]),
                               (139.0, 78.8), (83.0, 78.8),
                               (83.0, b.pad("J3", 2)[1]), b.pad("J3", 2)],
                 0.30, "In2.Cu", "vane-raw")
    b.add_via("VANE_RAW", b.pad("D8", 1), size=0.60, drill=0.30, tag="vane-esd")
    b.add_segment("VANE_RAW", b.pad("D8", 1), b.pad("R12", 2),
                  0.30, "In2.Cu", "vane-esd-branch")
    b.add_via("LDR_RAW", b.pad("R14", 2), size=0.60, drill=0.30, tag="ldr-raw-a")
    b.route_poly("LDR_RAW", [b.pad("R14", 2), (136.0, b.pad("R14", 2)[1]),
                              (136.0, b.pad("J6", 1)[1]), b.pad("J6", 1)],
                 0.30, "B.Cu", "ldr-raw")
    b.add_via("LDR_RAW", b.pad("D9", 1), size=0.60, drill=0.30, tag="ldr-esd")
    b.add_segment("LDR_RAW", b.pad("D9", 1), (136.0, b.pad("D9", 1)[1]), 0.30, "B.Cu", "ldr-esd-branch")

    straight("BATT_ADC", "C11", 2, "R17", 2, "batt-adc-a")
    straight("BATT_ADC", "R17", 2, "R16", 1, "batt-adc-b")
    for ref, padno, tag in (("R16", 2, "batt-source"), ("D10", 1, "batt-clamp")):
        b.add_via("BATT_SENSE_RAW", b.pad(ref, padno), size=0.60, drill=0.30, tag=tag)
    b.route_poly("BATT_SENSE_RAW", [b.pad("R16", 2), (127.25, 57.0),
                                     (130.0, 57.0), (130.0, b.pad("J7", 1)[1]),
                                     b.pad("J7", 1)],
                 0.30, "In2.Cu", "batt-raw")
    b.route_poly("BATT_SENSE_RAW", [b.pad("D10", 1), (133.0, b.pad("D10", 1)[1]),
                                     (133.0, 58.0), (127.25, 58.0), b.pad("R16", 2)],
                 0.30, "In2.Cu", "batt-clamp")

    # Ground vias for every SMD ground terminal. Bottom GND zone completes the connection.
    ground_smd_pads = [
        ("D1", 2), ("C1", 1), ("C2", 1), ("LED1", 1), ("U4", 2), ("C3", 1),
        ("C4", 1), ("U2", 1), ("U2", 8), ("U2", 10), ("C5", 1), ("C6", 1),
        ("D3", 2), ("D4", 2), ("D5", 2), ("D6", 2), ("D7", 2),
        ("D8", 2), ("D9", 2), ("R17", 1), ("C11", 1), ("D10", 2),
    ]
    for index, (ref, padno) in enumerate(ground_smd_pads):
        point = b.pad(ref, padno)
        # Via-in-pad is intentional on these hand-assembled prototype footprints;
        # it avoids long ground stubs and keeps each protection clamp local.
        b.add_via("GND", point, size=0.60, drill=0.30, tag=f"gnd-{ref}-{padno}-{index}")

    # Board outline with a notch under the ESP32-S3 Wi-Fi antenna.
    outline = [(20, 26), (27, 26), (27, 32.8), (76, 32.8), (76, 26), (150, 26),
               (150, 103), (20, 103), (20, 26)]
    for index in range(len(outline) - 1):
        b.graphics.append(
            f'  (gr_line (start {b.xy(*outline[index])}) (end {b.xy(*outline[index+1])}) '
            f'(stroke (width 0.25) (type solid)) (layer "Edge.Cuts") '
            f'(uuid "{uid(f"outline/{index}")}"))'
        )
    # Product and connector legend.
    texts = [
        ("AMR CENTRAL GATEWAY v1.1", (92, 28.0), 1.5),
        ("ESP32-S3 + RFM95W 868 MHz", (92, 33.8), 1.0),
        ("POWER 7-18V DC", (137, 27.6), 0.9),
        ("BAT SENSE MAX 6.0V", (136, 46.5), 0.8),
        ("ANTENNA", (141, 61.0), 0.8),
        ("NO COPPER / PCB UNDER WIFI ANTENNA", (51.5, 31.5), 0.8),
        ("U1 ESP32-S3 DEVKIT", (51.5, 63.0), 1.0),
        ("U2 RFM95W", (102.0, 49.0), 1.0),
        ("RAIN", (58.0, 98.0), 0.80),
        ("WIND", (82.0, 98.4), 0.80),
        ("J4 3V D G", (107.5, 101.0), 0.80),
        ("J5 3V DA CL G", (134.0, 101.0), 0.80),
        ("J6 LDR G", (142.0, 82.5), 0.80),
        ("J7 BAT G", (136.0, 48.0), 0.80),
        ("J10 PANEL SW", (82.0, 63.0), 0.80),
    ]
    for index, (label, at, size) in enumerate(texts):
        b.graphics.append(
            f'  (gr_text "{q(label)}" (at {b.xy(*at)}) (layer "F.SilkS") '
            f'(uuid "{uid(f"text/{index}")}") (effects (font (size {size:.2f} {size:.2f}) '
            f'(thickness 0.15)) (justify)))'
        )

    # Bottom copper ground plane. KiCad CLI refills and saves it during validation.
    zone_points = " ".join(f"(xy {x:.3f} {y:.3f})" for x, y in outline[:-1])
    zone_bottom = f'''  (zone (net {NET["GND"]}) (net_name "GND") (layer "B.Cu")
    (uuid "{uid('zone/gnd-bottom')}") (hatch edge 0.5)
    (connect_pads (clearance 0.25)) (min_thickness 0.25) (fill yes (thermal_gap 0.30) (thermal_bridge_width 0.35))
    (polygon (pts {zone_points})))'''
    zone_inner = f'''  (zone (net {NET["GND"]}) (net_name "GND") (layer "In1.Cu")
    (uuid "{uid('zone/gnd-inner')}") (hatch edge 0.5)
    (connect_pads (clearance 0.25)) (min_thickness 0.25) (fill yes (thermal_gap 0.30) (thermal_bridge_width 0.35))
    (polygon (pts {zone_points})))'''

    nets = os.linesep.join(f'  (net {number} "{q(name)}")' for name, number in NET.items())
    layers = '''  (layers
    (0 "F.Cu" signal "top_cu")
    (4 "In1.Cu" power "gnd_plane")
    (6 "In2.Cu" signal "signals")
    (2 "B.Cu" signal "bottom_cu")
    (9 "F.Adhes" user "F.Adhesive")
    (11 "B.Adhes" user "B.Adhesive")
    (13 "F.Paste" user)
    (15 "B.Paste" user)
    (5 "F.SilkS" user "F.Silkscreen")
    (7 "B.SilkS" user "B.Silkscreen")
    (1 "F.Mask" user)
    (3 "B.Mask" user)
    (17 "Dwgs.User" user "User.Drawings")
    (19 "Cmts.User" user "User.Comments")
    (21 "Eco1.User" user "User.Eco1")
    (23 "Eco2.User" user "User.Eco2")
    (25 "Edge.Cuts" user)
    (27 "Margin" user)
    (31 "F.CrtYd" user "F.Courtyard")
    (29 "B.CrtYd" user "B.Courtyard")
    (35 "F.Fab" user)
    (33 "B.Fab" user)
  )'''
    return f'''(kicad_pcb
  (version 20241229)
  (generator "pcbnew")
  (generator_version "9.0")
  (general (thickness 1.6) (legacy_teardrops no))
  (paper "A4")
{layers}
  (setup (pad_to_mask_clearance 0))
{nets}
{os.linesep.join(b.footprints)}
{os.linesep.join(b.graphics)}
{os.linesep.join(b.segments)}
{os.linesep.join(b.vias)}
{zone_bottom}
{zone_inner}
)'''


def legacy_symbol_library() -> str:
    def symbol(name: str, prefix: str, pins: list[tuple[str, str, int, int, str, str]]) -> str:
        lines = [f"#\n# {name}\n#", f"DEF {name} {prefix} 0 40 Y Y 1 F N", "F0 \"{prefix}\" 0 200 50 H V C CNN",
                 f"F1 \"{name}\" 0 -200 50 H V C CNN", "DRAW", "S -100 150 100 -150 0 1 10 f"]
        for pname, pnum, x, y, orient, ptype in pins:
            lines.append(f"X {pname} {pnum} {x} {y} 100 {orient} 40 40 1 1 {ptype}")
        lines += ["ENDDRAW", "ENDDEF"]
        return "\n".join(lines)

    parts = ["EESchema-LIBRARY Version 2.4", "#encoding utf-8"]
    two_pin = [("1", "1", -200, 0, "R", "P"), ("2", "2", 200, 0, "L", "P")]
    for name, prefix in (("R", "R"), ("C", "C"), ("D", "D"), ("FUSE", "F"), ("LED", "D"), ("SW", "SW")):
        parts.append(symbol(name, prefix, two_pin))
    for count in (2, 3, 4):
        pins = [(f"Pin_{i}", str(i), -300, (count + 1 - 2*i) * 75, "R", "P") for i in range(1, count + 1)]
        parts.append(symbol(f"CONN_1X{count}", "J", pins))
    parts.append(symbol("DCDC_3", "U", [("VIN", "1", -300, 100, "R", "P"),
        ("GND", "2", -300, -100, "R", "P"), ("VOUT", "3", 300, 100, "L", "P")]))
    parts.append(symbol("LDO_5", "U", [("VIN", "1", -300, 100, "R", "P"),
        ("GND", "2", -300, -100, "R", "P"), ("EN", "3", -300, 0, "R", "P"),
        ("NC", "4", 300, -100, "L", "N"), ("VOUT", "5", 300, 100, "L", "P")]))
    parts.append(symbol("SMA", "J", [("RF", "1", -300, 100, "R", "P"),
        ("GND", "2", -300, -100, "R", "P")]))
    parts.append(symbol("TESTPOINT", "TP", [("TP", "1", -200, 0, "R", "P")]))

    # DevKit symbol mirrors the PCB pad numbering.
    left_names = ["3V3", "3V3", "EN", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO15",
                  "GPIO16", "GPIO17", "GPIO18", "GPIO8", "GPIO3", "GPIO46", "GPIO9",
                  "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "5V", "GND"]
    right_names = ["GND", "GPIO43", "GPIO44", "GPIO1", "GPIO2", "GPIO42", "GPIO41", "GPIO40",
                   "GPIO39", "GPIO38", "GPIO37", "GPIO36", "GPIO35", "GPIO0", "GPIO45", "GPIO48",
                   "GPIO47", "GPIO21", "GPIO20", "GPIO19", "GND", "GND"]
    pins = []
    for index, pname in enumerate(left_names):
        pins.append((pname, str(index + 1), -900, 1050 - index * 100, "R", "P"))
    for index, pname in enumerate(right_names):
        pins.append((pname, str(44 - index), 900, 1050 - index * 100, "L", "P"))
    lines = ["#\n# ESP32_S3_DEVKITC_1\n#", "DEF ESP32_S3_DEVKITC_1 U 0 40 Y Y 1 F N",
             "F0 \"U\" 0 1200 50 H V C CNN", "F1 \"ESP32-S3-DevKitC-1\" 0 -1200 50 H V C CNN",
             "DRAW", "S -600 1100 600 -1100 0 1 12 f"]
    lines += [f"X {pn} {num} {x} {y} 300 {orient} 35 35 1 1 {ptype}" for pn, num, x, y, orient, ptype in pins]
    lines += ["ENDDRAW", "ENDDEF"]
    parts.append("\n".join(lines))

    rfm_names = ["GND", "MISO", "MOSI", "SCK", "NSS", "RESET", "DIO5", "GND",
                 "ANT", "GND", "DIO3", "DIO4", "3V3", "DIO0", "DIO1", "DIO2"]
    lines = ["#\n# RFM95W\n#", "DEF RFM95W U 0 40 Y Y 1 F N", "F0 \"U\" 0 550 50 H V C CNN",
             "F1 \"RFM95W-868S2\" 0 -550 50 H V C CNN", "DRAW", "S -400 400 400 -400 0 1 12 f"]
    for number in range(1, 9):
        lines.append(f"X {rfm_names[number-1]} {number} -700 {350-(number-1)*100} 300 R 40 40 1 1 P")
    for index, number in enumerate(range(16, 8, -1)):
        lines.append(f"X {rfm_names[number-1]} {number} 700 {350-index*100} 300 L 40 40 1 1 P")
    lines += ["ENDDRAW", "ENDDEF"]
    parts.append("\n".join(lines))
    parts += ["#End Library"]
    return "\n".join(parts) + "\n"


def component(lib: str, ref: str, value: str, footprint: str, x: int, y: int,
              orientation: tuple[int, int, int, int] = (1, 0, 0, -1)) -> str:
    timestamp = uid(f"sch/{ref}").replace("-", "")[:8].upper()
    a, b, c, d = orientation
    return f'''$Comp
L AMR_Central:{lib} {ref}
U 1 1 {timestamp}
P {x} {y}
F 0 "{ref}" H {x} {y-250} 50  0000 C CNN
F 1 "{value}" H {x} {y+250} 50  0000 C CNN
F 2 "{footprint}" H {x} {y} 50  0001 C CNN
F 3 "" H {x} {y} 50  0001 C CNN
	1    {x} {y}
	{a}    {b}    {c}    {d}
$EndComp'''


def text_label(x: int, y: int, label: str, orientation: int = 0) -> str:
    return f"Text Label {x} {y} {orientation}    40   ~ 0\n{label}"


def no_connect(x: int, y: int) -> str:
    return f"NoConn ~ {x} {y}"


def build_legacy_schematic(bom: list[dict[str, str]]) -> str:
    blocks = [
        "EESchema Schematic File Version 4", "LIBS:AMR_Central", "EELAYER 29 0", "EELAYER END",
        "$Descr A4 11693 8268", "encoding utf-8", "Sheet 1 1", 'Title "AMR Central Gateway ESP32-S3 Carrier"',
        'Date "2026-08-08"', 'Rev "1.0-prototype"', 'Comp "AMR IoT Platform"',
        'Comment1 "7-18V input; RFM95W 868MHz; protected weather interfaces"',
        'Comment2 "Firmware pin map: src/central/main.cpp"', 'Comment3 "Prototype: engineering review required before sale"',
        'Comment4 "Four-layer, 1.6mm FR-4; In1 and bottom GND planes"', "$EndDescr",
        "Text Notes 700 700 0    100  ~ 20\nCENTRAL GATEWAY - ESP32-S3 DEVKITC-1 CARRIER",
        "Text Notes 700 900 0    55   ~ 0\nAll GPIO names and connector nets match the compiled central firmware.",
    ]

    # Controller and radio.
    blocks.append(component("ESP32_S3_DEVKITC_1", "U1", "ESP32-S3-DevKitC-1-N8R8",
                            "AMR_Central:ESP32-S3-DevKitC-1_Carrier", 3000, 3800))
    left_labels = {4:"I2C_SDA_GPIO",5:"I2C_SCL_GPIO",6:"RAIN_GPIO",7:"DHT_GPIO",11:"LORA_NSS",
                   12:"BATT_ADC",13:"WIND_GPIO",15:"LDR_ADC",16:"VANE_ADC",17:"LORA_MOSI",
                   18:"LORA_SCK",19:"LORA_MISO",20:"LORA_RST",21:"+5V",22:"GND"}
    right_labels = {44:"GND",43:"UART_TX",42:"UART_RX",40:"LORA_DIO0",31:"SETUP_RESET",24:"GND",23:"GND"}
    for number in range(1, 23):
        x, y = 2100, 2750 + (number - 1) * 100
        blocks.append(text_label(x, y, left_labels[number]) if number in left_labels else no_connect(x, y))
    for index, number in enumerate(range(44, 22, -1)):
        x, y = 3900, 2750 + index * 100
        blocks.append(text_label(x, y, right_labels[number], 2) if number in right_labels else no_connect(x, y))

    blocks.append(component("RFM95W", "U2", "RFM95W-868S2", "AMR_Central:HOPERF_RFM95W_SMD", 6200, 3400))
    rfm_left = {1:"GND",2:"LORA_MISO",3:"LORA_MOSI",4:"LORA_SCK",5:"LORA_NSS",6:"LORA_RST",8:"GND"}
    rfm_right = {15:"LORA_DIO1",14:"LORA_DIO0",13:"+3V3_PERIPH",10:"GND",9:"LORA_ANT"}
    for number in range(1, 9):
        x, y = 5500, 3050 + (number - 1) * 100
        blocks.append(text_label(x, y, rfm_left[number]) if number in rfm_left else no_connect(x, y))
    for index, number in enumerate(range(16, 8, -1)):
        x, y = 6900, 3050 + index * 100
        blocks.append(text_label(x, y, rfm_right[number], 2) if number in rfm_right else no_connect(x, y))
    blocks.append(component("SMA", "J9", "SMA 868MHz", "AMR_Central:SMA_Vertical_THT", 7800, 3800))
    blocks += [text_label(7500, 3700, "LORA_ANT"), text_label(7500, 3900, "GND")]

    # Power chain.
    blocks.append("Text Notes 5100 900 0    70   ~ 12\nPROTECTED FIELD POWER")
    blocks.append(component("CONN_1X2", "J1", "POWER 7-18V DC", "AMR_Central:TerminalBlock_2", 5200, 1300))
    blocks += [text_label(4900, 1225, "VIN_RAW"), text_label(4900, 1375, "GND")]
    power_parts = [
        ("FUSE", "F1", "PTC 0.75A", "AMR_Central:Fuse_1812", 6000, 1200, "VIN_RAW", "VIN_FUSED"),
        ("D", "D2", "SS34", "AMR_Central:D_SMA", 6800, 1200, "VIN_FUSED", "VIN_PROTECTED"),
        ("R", "R1", "1k", "AMR_Central:R_0805", 8400, 1200, "+5V", "PWR_LED_A"),
        ("LED", "LED1", "GREEN", "AMR_Central:LED_0805", 9000, 1200, "PWR_LED_A", "GND"),
    ]
    for lib, ref, value, fp, x, y, n1, n2 in power_parts:
        blocks += [component(lib, ref, value, fp, x, y), text_label(x-200, y, n1), text_label(x+200, y, n2, 2)]
    blocks.append(component("DCDC_3", "U3", "R-78E5.0-1.0", "AMR_Central:RECOM_R-78E5.0-1.0_SIP3", 7600, 1200))
    blocks += [text_label(7300, 1100, "VIN_PROTECTED"), text_label(7300, 1300, "GND"), text_label(7900, 1100, "+5V", 2)]
    blocks.append(component("LDO_5", "U4", "AP2112K-3.3", "AMR_Central:SOT-23-5", 7600, 1800))
    blocks += [text_label(7300, 1700, "+5V"), text_label(7300, 1800, "+5V"), text_label(7300, 1900, "GND"),
               no_connect(7900, 1900), text_label(7900, 1700, "+3V3_PERIPH", 2)]
    for ref, value, x, y, n1, n2 in (
        ("D1","SMBJ18A",6200,1700,"VIN_FUSED","GND"), ("C1","10uF/35V",6800,1700,"VIN_PROTECTED","GND"),
        ("C2","22uF/10V",8400,1700,"+5V","GND"), ("C3","1uF",9000,1700,"+5V","GND"),
        ("C4","1uF",9600,1700,"+3V3_PERIPH","GND"), ("C5","10uF",7200,3000,"+3V3_PERIPH","GND"),
        ("C6","100nF",7800,3000,"+3V3_PERIPH","GND")):
        lib = "D" if ref == "D1" else "C"
        blocks += [component(lib, ref, value, f"AMR_Central:{'D_SMB' if ref == 'D1' else 'C_0805'}", x, y),
                   text_label(x-200, y, n1), text_label(x+200, y, n2, 2)]

    # Interface symbols use labels rather than long wires to keep the sheet readable.
    blocks.append("Text Notes 7300 4550 0    70   ~ 12\nFIELD SENSOR INTERFACES")
    interface_specs = [
        ("J2", "RAIN RJ11", 7600, 5000, [None,None,"GND","RAIN_RAW",None,None]),
        ("J3", "WIND+VANE RJ11", 7600, 5900, [None,"VANE_RAW","GND","WIND_RAW","GND",None]),
        ("J4", "DHT11", 7600, 6050, ["+3V3_PERIPH","DHT_RAW","GND"]),
        ("J5", "BMP280 I2C", 9600, 5000, ["+3V3_PERIPH","I2C_SDA_RAW","I2C_SCL_RAW","GND"]),
        ("J6", "PASSIVE LDR", 9600, 5700, ["LDR_RAW","GND"]),
        ("J7", "BAT SENSE 0-6V", 9600, 6400, ["BATT_SENSE_RAW","GND"]),
        ("J8", "SERVICE UART", 5200, 5200, ["+3V3_PERIPH","GND","UART_TX","UART_RX"]),
        ("J10", "PANEL SETUP BUTTON", 5200, 4700, ["SETUP_RESET","GND"]),
    ]
    for ref, value, x, y, nets in interface_specs:
        footprint = ("AMR_Central:WR-MJ_615006138421" if ref in {"J2", "J3"}
                     else "AMR_Central:JST_XH_B2B-XH-A_1x02_P2.50mm" if ref == "J10"
                     else f"AMR_Central:Connector_{len(nets)}")
        blocks.append(component(f"CONN_1X{len(nets)}", ref, value, footprint, x, y))
        for index, net in enumerate(nets):
            py = y + (len(nets) + 1 - 2 * (index + 1)) * 75
            blocks.append(text_label(x-300, py, net) if net else no_connect(x-300, py))

    # All two-pin conditioning parts shown compactly in a table-like block.
    cond_parts = [row for row in bom if row["Reference"].startswith(("R", "C", "D")) and row["Reference"] not in {"R1","C1","C2","C3","C4","C5","C6","D1","D2"}]
    x0, y0 = 5000, 5800
    for index, row in enumerate(cond_parts):
        col, line = index // 8, index % 8
        x, y = x0 + col * 1000, y0 + line * 260
        blocks.append(component("R" if row["Reference"].startswith("R") else "C" if row["Reference"].startswith("C") else "D",
                                row["Reference"], row["Value"], row["Footprint"], x, y))
    blocks.append("Text Notes 700 7200 0    55   ~ 0\nBAT SENSE is limited to 0-6.0V because firmware uses a 100k/100k divider and 2.0 multiplier.")
    blocks.append("Text Notes 700 7350 0    55   ~ 0\nUse a tuned 868MHz antenna. Do not transmit without the antenna connected.")
    blocks.append("Text Notes 700 7500 0    55   ~ 0\nThe enclosure-mounted normally-open setup button connects to J10; no switch is fitted on the PCB.")
    blocks.append("$EndSCHEMATC")
    return "\n".join(blocks) + "\n"


def make_bom() -> list[dict[str, str]]:
    rows: list[tuple[str, str, str, str, str, str]] = [
        ("U1","ESP32-S3-DevKitC-1-N8R8","Espressif ESP32-S3 development board","ESP32-S3-DEVKITC-1-N8R8","AMR_Central_ESP32-S3-DevKitC-1_Carrier","1"),
        ("U2","RFM95W-868S2","HopeRF 868/915MHz LoRa module","RFM95W-868S2","AMR_Central_HOPERF_RFM95W_SMD","1"),
        ("U3","R-78E5.0-1.0","1A 5V SIP switching regulator","R-78E5.0-1.0","AMR_Central_RECOM_R-78E5.0-1.0_SIP3","1"),
        ("U4","AP2112K-3.3","600mA low-noise 3.3V LDO","AP2112K-3.3TRG1","AMR_Central_SOT-23-5","1"),
        ("J1,J6,J7","2-pin 5.08mm terminal block","Pluggable or fixed terminal block","Generic","AMR_Central_TerminalBlock_2","3"),
        ("J2,J3","615006138421","Horizontal six-position RJ11-compatible modular jack, tab up","Wuerth Elektronik 615006138421","AMR_Central_WR-MJ_615006138421","2"),
        ("J4","3-pin 5.08mm terminal block","Pluggable or fixed terminal block","Generic","AMR_Central_TerminalBlock_3","1"),
        ("J5","4-pin 5.08mm terminal block","Pluggable or fixed terminal block","Generic","AMR_Central_TerminalBlock_4","1"),
        ("J8","1x4 2.54mm header","Service UART header","Generic","AMR_Central_Header_1x4","1"),
        ("J9","SMA female vertical","868MHz antenna connector","Generic","AMR_Central_SMA_Vertical_THT","1"),
        ("J10","B2B-XH-A(LF)(SN)","2-pin 2.50mm locking header for enclosure button harness","JST B2B-XH-A(LF)(SN)","AMR_Central_JST_XH_B2B-XH-A_1x02_P2.50mm","1"),
        ("ENC-SW1","Panel momentary NO button","Enclosure-mounted setup/reset button wired to J10","Select for enclosure/IP rating","OFF_BOARD","1"),
        ("F1","PTC 0.75A","1812 resettable fuse","MF-MSMF075-2","AMR_Central_Fuse_1812","1"),
        ("D1","SMBJ18A","600W unidirectional TVS","SMBJ18A","AMR_Central_D_SMB","1"),
        ("D2","SS34","3A 40V Schottky diode","SS34","AMR_Central_D_SMA","1"),
        ("D3-D9","PESD3V3","3.3V ESD protection diode","PESD3V3U1BA","AMR_Central_D_SOD323","7"),
        ("D10","BZT52C6V2","6.2V Zener clamp","BZT52C6V2","AMR_Central_D_SOD323","1"),
        ("LED1","GREEN","0805 green LED","Generic","AMR_Central_LED_0805","1"),
        ("R1,R2,R4,R12,R14","1k","0805 1% resistor","Generic","AMR_Central_R_0805","5"),
        ("R3,R5,R13,R15","10k","0805 1% resistor","Generic","AMR_Central_R_0805","4"),
        ("R6","220R","0805 1% resistor","Generic","AMR_Central_R_0805","1"),
        ("R7,R10,R11","4.7k","0805 1% resistor","Generic","AMR_Central_R_0805","3"),
        ("R8,R9","100R","0805 1% resistor","Generic","AMR_Central_R_0805","2"),
        ("R16,R17","100k 1%","0805 precision divider","Generic","AMR_Central_R_0805","2"),
        ("C1","10uF 35V","1210 X7R input capacitor","Generic","AMR_Central_C_1210","1"),
        ("C2","22uF 10V","1206 X7R output capacitor","Generic","AMR_Central_C_1206","1"),
        ("C3,C4","1uF","0805 X7R capacitor","Generic","AMR_Central_C_0805","2"),
        ("C5","10uF","0805 X7R LoRa bulk capacitor","Generic","AMR_Central_C_0805","1"),
        ("C6,C11","100nF","0805 X7R capacitor","Generic","AMR_Central_C_0805","2"),
        ("TP1-TP4","Test points","Through-hole loop/test point","Generic","AMR_Central_TestPoint_THT","4"),
    ]
    return [dict(zip(("Reference","Value","Description","Manufacturer Part Number","Footprint","Quantity"), row)) for row in rows]


def write_outputs() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    bom = make_bom()
    (ROOT / "AMR_Central.lib").write_text(legacy_symbol_library(), encoding="utf-8", newline="\n")
    (ROOT / f"{PROJECT}.sch").write_text(build_legacy_schematic(bom), encoding="utf-8", newline="\n")
    (ROOT / f"{PROJECT}.kicad_pcb").write_text(build_board(), encoding="utf-8", newline="\n")
    (ROOT / "sym-lib-table").write_text(
        '(sym_lib_table\n  (version 7)\n  (lib (name "AMR_Central")(type "Legacy")'
        '(uri "${KIPRJMOD}/AMR_Central.lib")(options "")(descr "AMR central gateway symbols"))\n)\n',
        encoding="utf-8", newline="\n")
    # The routed board embeds its purpose-built footprints. Keep the project
    # table valid and empty instead of referencing a nonexistent global library.
    (ROOT / "fp-lib-table").write_text('(fp_lib_table\n  (version 7)\n)\n',
                                        encoding="utf-8", newline="\n")
    project = {
        "board": {}, "boards": [], "cvpcb": {}, "erc": {}, "libraries": {},
        "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1},
        "net_settings": {"classes": [], "meta": {"version": 3}},
        "pcbnew": {}, "schematic": {}, "sheets": [], "text_variables": {},
    }
    (ROOT / f"{PROJECT}.kicad_pro").write_text(json.dumps(project, indent=2) + "\n", encoding="utf-8")
    with (ROOT / "BOM.csv").open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(bom[0]))
        writer.writeheader()
        writer.writerows(bom)


if __name__ == "__main__":
    write_outputs()
    print(f"Generated KiCad sources in {ROOT}")

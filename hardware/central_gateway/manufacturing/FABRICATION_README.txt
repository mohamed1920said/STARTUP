AMR CENTRAL GATEWAY v1.1-prototype
==================================

Board: 130 mm x 77 mm maximum outline, with ESP32 antenna notch
Material: FR-4
Finished thickness: 1.6 mm
Copper layers: 4
Suggested copper: 1 oz external and internal
Surface finish: ENIG preferred for prototype assembly; lead-free HASL acceptable

Layer order
-----------
L1  central_gateway-top_cu.gtl          Top copper
L2  central_gateway-gnd_plane.g1        Continuous GND plane
L3  central_gateway-signals.g2          Internal signals
L4  central_gateway-bottom_cu.gbl       Bottom copper with GND pour

Connector-specific notes
------------------------
J2 and J3 use the manufacturer footprint for Wuerth Elektronik 615006138421,
horizontal six-position RJ11-compatible modular jack, tab up. J10 is a two-pin JST XH header
for the enclosure-mounted normally-open setup/reset button; no PCB switch is
fitted. U2 is a soldered RFM95W-868S2 radio module and J9 is its SMA antenna port.

The archive includes front/back solder mask, front/back silkscreen, edge cuts,
a Gerber job file, separate plated and non-plated Excellon drill files, drill
maps/report, BOM, placement CSV, and the clean KiCad DRC report.

Important RF note
-----------------
The 0.45 mm antenna trace is a prototype starting value. It is NOT certified as
50 ohms for an arbitrary stack-up. Recalculate it from the fabricator's actual
L1-to-L2 dielectric thickness and dielectric constant, then validate the
assembled antenna path in its enclosure with a VNA.

Fabricator checks requested
---------------------------
1. Confirm all four copper layers and their order before manufacture.
2. Keep the ESP32 antenna notch exactly as defined by Edge.Cuts.
3. Keep NPTH mounting and connector locating holes non-plated.
4. Do not modify the RF trace or fill the antenna notch with copper.
5. Confirm the 0.90 mm contact drills and 2.36 mm locating holes for J2/J3.
6. Report annular-ring, solder-mask-web, or drill capability exceptions before production.

Release status
--------------
KiCad 10 DRC: 0 violations, 0 unconnected pads.
Firmware central_gateway build: successful after 16-direction vane calibration.
This package is for prototype/pilot fabrication and still requires engineering,
RF, EMC, surge, thermal, enclosure, and regulatory validation before sale.

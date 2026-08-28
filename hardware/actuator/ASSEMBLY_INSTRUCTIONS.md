# Assembly Instructions

## 1. Preparation

Deburr every metal part. Mask bearing seats and friction faces before surface treatment. Clean printed gears, remove stringing between teeth, and verify the 90.00 mm centre distance on the real plates. Do not force bearings into an undersize printed or anodized bore.

Record the actual motor shaft diameter, D-flat, shaft projection, body diameter, body length, and mounting holes. Record the valve neck/body dimensions, stem profile, stem height, and breakaway torque in both directions before machining the final bracket or adapter.

## 2. Bearing and shaft stack

1. Machine each aluminium carrier to a 47 H7 bearing bore after anodizing allowance is considered.
2. Add the drawing-specified internal retaining grooves or retaining plates.
3. Press or slip-fit one 6204-2RS into each carrier using force only on the outer race.
4. Bolt the lower carrier above the lower plate and the upper carrier below the upper plate.
5. Pass the 20 h6 shaft through both bearings. Confirm free rotation and 1-2 mm valve-stem axial clearance.
6. Bolt the measured stem adapter to the shaft flange with four M6 fasteners and mechanical locking.

Rev-B stack heights from the lower-plate top face are: lower carrier Z=6-22 mm, clutch hub Z=22-48 mm, output gear Z=28-48 mm, pressure plate Z=48-53 mm, Belleville stack Z=53-65 mm, upper carrier Z=65-81 mm, and upper plate Z=81-87 mm. Do not compress this stack by moving the upper carrier into the clutch components.

## 3. Gear and clutch stack

1. Fit a metal D-insert to the 40T motor gear. Do not depend on a printed bore and set screw alone.
2. Fit the metal sleeve/bush in the 80T gear.
3. Assemble the output clutch from bottom to top: keyed output hub, lower friction washer, 80T gear, upper friction washer, steel pressure plate, Belleville stack, M16 prevailing-torque locknut.
4. Set the gear centres to 90.00 mm. Use the motor slots to establish 0.25-0.35 mm practical backlash without tight spots through a full revolution.
5. Phase the 80T gear by one-half tooth pitch (2.25 degrees) relative to the zero-angle 40T gear before checking mesh.
6. Apply plastic-compatible PTFE grease sparingly to the teeth. Keep grease away from clutch friction surfaces.
7. Start clutch calibration at 8 N m. Final setting is 1.5 times measured wet breakaway torque only if that value remains below the verified safe limit of the valve stem, gears, shaft flange, and gearbox.

## 4. Limits and feedback

1. Install the AS5600 on a nonmagnetic bracket directly above the final output shaft.
2. Centre a diametrically magnetized magnet on the shaft and set the module manufacturer's air gap.
3. Calibrate CLOSED and OPEN angles from the real valve, not from motor turns.
4. Set reeds to confirm approximately 2 and 88 degrees.
5. Set normally-closed hard limits near 0 and 90 degrees with at least +/-5 degrees bracket adjustment.
6. Set M8 steel stops at approximately -1 and 91 degrees. Verify the stop block, not the PVC pipe, absorbs reaction torque.
7. Confirm the protection order: AS5600 target, reed, hard limit, steel stop, clutch slip.

## 5. Manual override

Cross-pin or key the 30 mm external-hex override to the final shaft. The manual-mode control must remove BTS7960 enable power through a normally-closed hardware interlock before the tool or handle can engage. The firmware must also report manual mode.

## 6. Valve clamp

Fit a 3 mm EPDM liner between the two bridge supports and the valve body/neck. Tighten four stainless M8 clamp bolts evenly. Do not clamp thin pipe spans or distort the PVC valve body. The 63 mm conversion changes the lower clamp and measured stem adapter only.

## 7. Electronics and enclosure

Keep the battery, TTGO board, BMS, fuse, and motor driver isolated from moving gears. Route motor wires separately from I2C and sensor wires. Place the antenna away from the motor, shaft, and steel fasteners. Use strain-relieved cable glands, a gasketed cover, a hydrophobic pressure vent, and a drainage path outside the sealed electronics chamber.

Install the three 18650 cells horizontally on the battery tray; do not stand them vertically inside the gear cover. The enclosure is installed open-side down with its roof at Z=164-170 mm.

## 8. Commissioning tests

1. Bench test with the valve removed and a current-limited supply.
2. Confirm both hard limits interrupt motion in the hazardous direction while reverse motion remains possible.
3. Confirm manual mode disables the driver in hardware.
4. Measure no-load, running, breakaway, stall, and clutch-slip current.
5. Install the real valve and repeat at minimum and maximum expected temperature, dry and wet.
6. Complete 500 bench cycles, then 5,000 endurance cycles before a sales pilot.
7. Perform water-ingress, condensation, UV, corrosion, brownout, blocked-valve, sensor-disconnect, and radio-loss tests.

## 9. SolidWorks Motion Study

Open `cad/native/AMR_50mm_Valve_Actuator.SLDASM`, select the `AMR_Valve_Open_Close` study, and calculate it before playback. It is a 6-second Basic Motion study with 10 rotary motor features: the 40T motor gear moves through 180 degrees while the 80T output group moves through 90 degrees in the opposite direction. The enclosure cover is visible in the saved assembly; suppress or hide it temporarily to inspect the drivetrain.

Run `tools/build_motion_study.ps1` after changing any native part, placement, or motor definition. Release only when `cad/motion/AMR_Valve_Open_Close_REPORT.txt` ends with `RESULT=PASS`, contains 35 resolved components and 10 features after calculation, and reports less than 0.01 mm gear-centre translation. The Motion Study is a kinematic design check and does not replace physical torque, clutch-slip, limit-switch, blocked-valve, or endurance testing.

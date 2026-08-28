Option Explicit

' AMR 50 mm quarter-turn valve actuator native SolidWorks generator.
' All geometry uses SI units at the API boundary (metres).

Const swDocPART = 1
Const swDocASSEMBLY = 2
Const swSaveAsCurrentVersion = 0
Const swSaveAsOptions_Silent = 1
Const PI = 3.1415926535897931

Dim fso, projectRoot, outRoot, nativeDir, stepDir, stlDir, renderDir
Dim swApp, partTemplate, assemblyTemplate
Set fso = CreateObject("Scripting.FileSystemObject")

If WScript.Arguments.Count < 1 Then
    WScript.Echo "Usage: cscript //nologo generate_solidworks.vbs PROJECT_ROOT"
    WScript.Quit 2
End If

projectRoot = fso.GetAbsolutePathName(WScript.Arguments(0))
outRoot = fso.BuildPath(projectRoot, "hardware\actuator\cad")
nativeDir = fso.BuildPath(outRoot, "native")
stepDir = fso.BuildPath(outRoot, "step")
stlDir = fso.BuildPath(outRoot, "stl")
renderDir = fso.BuildPath(outRoot, "renders")
EnsureFolder outRoot
EnsureFolder nativeDir
EnsureFolder stepDir
EnsureFolder stlDir
EnsureFolder renderDir

partTemplate = "C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\Part.PRTDOT"
assemblyTemplate = "C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\Assembly.ASMDOT"
If Not fso.FileExists(partTemplate) Then Err.Raise vbObjectError + 100, , "Part template not found: " & partTemplate
If Not fso.FileExists(assemblyTemplate) Then Err.Raise vbObjectError + 101, , "Assembly template not found: " & assemblyTemplate

Set swApp = CreateObject("SldWorks.Application")
swApp.Visible = False
swApp.UserControl = False

WScript.Echo "SOLIDWORKS " & swApp.RevisionNumber
WScript.Echo "Generating AMR actuator native CAD..."

If WScript.Arguments.Count > 1 Then
    If LCase(WScript.Arguments(1)) = "assembly-only" Then
        On Error Resume Next
        CreateAssembly
        Dim assemblyErrorNumber, assemblyErrorDescription
        assemblyErrorNumber = Err.Number
        assemblyErrorDescription = Err.Description
        Err.Clear
        swApp.ExitApp
        Set swApp = Nothing
        On Error GoTo 0
        If assemblyErrorNumber <> 0 Then
            WScript.Echo "Assembly-only generation failed: " & CStr(assemblyErrorNumber) & " " & assemblyErrorDescription
            WScript.Quit 1
        End If
        WScript.Echo "Assembly-only generation complete: " & outRoot
        WScript.Quit 0
    End If
End If

' Structural and power-transmission parts.
CreatePlate "01_Lower_Base_Plate", 240, 180, 6, Array( _
    Array(45, 0, 47.1), Array(-45, 0, 60), Array(-105, -75, 8.5), Array(105, -75, 8.5), _
    Array(-105, 75, 8.5), Array(105, 75, 8.5), Array(-60, -38, 6.5), _
    Array(-60, 38, 6.5), Array(-30, -38, 6.5), Array(-30, 38, 6.5)), _
    "6061-T6 aluminium", "Clear anodize; mask bearing seat", "Production structural plate", False

CreatePlate "02_Upper_Support_Plate", 210, 160, 6, Array( _
    Array(45, 0, 47.1), Array(-90, -65, 8.5), Array(90, -65, 8.5), _
    Array(-90, 65, 8.5), Array(90, 65, 8.5), Array(29, -22, 4.5), _
    Array(61, -22, 4.5), Array(29, 22, 4.5), Array(61, 22, 4.5)), _
    "6061-T6 aluminium", "Clear anodize; mask bearing seat", "Upper bearing and sensor support", False

CreateMotorBracket
ImportGearFromStep "04_Motor_Gear_40T", 40, "PA12-CF", "Printed flat; hub solid; metal D-insert required"
ImportGearFromStep "05_Output_Gear_80T", 80, "PA12-CF", "Printed flat; clutch faces machined flat after print"
CreateOutputShaft
CreateOutputGearHub
CreateCylinderPart "08_Clutch_Pressure_Plate", 62, 5, 20.4, "C45 steel", "Ground friction faces; zinc plate elsewhere", "Clutch pressure plate", False
CreateCylinderPart "09_Belleville_Stack_Reference", 34, 12, 20.4, "Spring steel", "Supplier finish", "REFERENCE - select stack for 530 N initial preload", False
CreateStemAdapter
CreateClamp "11_Valve_Clamp_50mm", 50, 72
CreateClamp "12_Valve_Clamp_63mm", 63, 88

' Sensors, hard limits, and manual control.
CreatePlate "13_AS5600_Bracket", 64, 52, 4, Array( _
    Array(0, 0, 10), Array(-22, -16, 4), Array(22, -16, 4), _
    Array(-22, 16, 4), Array(22, 16, 4)), _
    "PA12-CF or 2 mm 6061", "Nonmagnetic", "Slotted vertical adjustment added at installation", True
CreateCylinderPart "14_Magnet_Carrier", 30, 8, 20.2, "PA12-CF", "As printed", "For diametrically magnetized 6 x 2.5 mm magnet", True
CreateCamRing
CreateSlottedBracket "16_Reed_Bracket_OPEN", 52, 20, 3, 4.5, "OPEN reed adjustable bracket"
CreateSlottedBracket "17_Reed_Bracket_CLOSED", 52, 20, 3, 4.5, "CLOSED reed adjustable bracket"
CreateSlottedBracket "18_Microswitch_Bracket_OPEN", 70, 32, 4, 5.5, "OPEN hard-limit switch bracket; +/-5 degree adjustment"
CreateSlottedBracket "19_Microswitch_Bracket_CLOSED", 70, 32, 4, 5.5, "CLOSED hard-limit switch bracket; +/-5 degree adjustment"
CreatePlate "20_Mechanical_Stop_Bracket", 112, 34, 10, Array( _
    Array(-42, 0, 8.5), Array(42, 0, 8.5), Array(-22, 0, 8.5), Array(22, 0, 8.5)), _
    "6061-T6 aluminium or steel", "Hard anodize or zinc plate", "M8 adjustable stop structure", False

' Electronics service trays and enclosure.
CreateTray "21_Battery_Tray", 78, 72, 4, Array(Array(-24, -26, 3.5), Array(24, -26, 3.5), Array(-24, 26, 3.5), Array(24, 26, 3.5)), "3 x 18650 3S1P removable tray"
CreateTray "22_TTGO_Tray", 128, 66, 4, Array(Array(-52, -25, 3.2), Array(52, -25, 3.2), Array(-52, 25, 3.2), Array(52, 25, 3.2)), "TTGO LoRa32 removable tray; USB edge clearance"
CreateTray "23_Motor_Driver_Tray", 118, 68, 4, Array(Array(-48, -25, 3.5), Array(48, -25, 3.5), Array(-48, 25, 3.5), Array(48, 25, 3.5)), "BTS7960 / IBT-2 tray with 25 mm heatsink clearance"
CreateManualOverride
CreateEnclosureCover
CreateBearingCarrier "26_Lower_Bearing_Carrier", "Lower 6204 bearing carrier"
CreateBearingCarrier "27_Upper_Bearing_Carrier", "Upper 6204 bearing carrier"

' Standard/reference components make the assembly intelligible but are not released for manufacture.
CreateCylinderPart "REF_6204_2RS_Bearing", 47, 14, 20, "Bearing steel / NBR", "Purchased", "REFERENCE ISO 6204-2RS", False
CreateMotorReference
CreatePlate "REF_AS5600_PCB", 23, 23, 1.6, Array(), "FR-4", "Purchased", "REFERENCE breakout envelope; measure mounting holes", False
CreatePlate "REF_TTGO_LORA32", 120, 55, 1.6, Array(), "FR-4", "Purchased", "REFERENCE - measure actual board and mounting holes", False
CreatePlate "REF_BTS7960_IBT2", 100, 55, 1.6, Array(), "FR-4 / aluminium heatsink", "Purchased", "REFERENCE - measure actual module and mounting holes", False
CreateCylinderPart "REF_18650_Cell", 18.6, 65.4, 0, "Li-ion cell", "Purchased insulated cell", "REFERENCE maximum cell envelope", False

If WScript.Arguments.Count > 1 Then
    If LCase(WScript.Arguments(1)) <> "parts-only" Then CreateAssembly
Else
    CreateAssembly
End If

swApp.ExitApp
Set swApp = Nothing
WScript.Echo "Generation complete: " & outRoot

Sub EnsureFolder(ByVal path)
    Dim parent
    If fso.FolderExists(path) Then Exit Sub
    parent = fso.GetParentFolderName(path)
    If Len(parent) > 0 And Not fso.FolderExists(parent) Then EnsureFolder parent
    fso.CreateFolder path
End Sub

Function M(ByVal mm)
    M = CDbl(mm) / 1000.0
End Function

Sub AddReleaseProperties(ByRef model, ByVal description, ByVal material, ByVal finish, ByVal printed)
    Dim cp
    Set cp = model.Extension.CustomPropertyManager("")
    cp.Add3 "Project", 30, "AMR 50/63 mm Irrigation Valve Actuator", 2
    cp.Add3 "Description", 30, description, 2
    cp.Add3 "Material", 30, material, 2
    cp.Add3 "Finish", 30, finish, 2
    cp.Add3 "Revision", 30, "A-PROTOTYPE", 2
    cp.Add3 "ReleaseStatus", 30, "PROTOTYPE - NOT RELEASED FOR PRODUCTION", 2
    cp.Add3 "CriticalHold", 30, "MEASURE REAL MOTOR SHAFT/BODY AND VALVE STEM/NECK BEFORE RELEASE", 2
    If printed Then
        cp.Add3 "ManufacturingProcess", 30, "FDM print; 0.6 mm nozzle; 8 walls; 75% infill; hub 100%", 2
    Else
        cp.Add3 "ManufacturingProcess", 30, "Machine/laser cut per drawing", 2
    End If
End Sub

Sub AddGlobals(ByRef model, ByVal valveDia)
    On Error Resume Next
    Dim eq
    Set eq = model.GetEquationMgr
    eq.Add2 -1, Chr(34) & "ValveDiameter" & Chr(34) & " = " & CStr(valveDia) & "mm", True
    eq.Add2 -1, Chr(34) & "MotorGearTeeth" & Chr(34) & " = 40", True
    eq.Add2 -1, Chr(34) & "OutputGearTeeth" & Chr(34) & " = 80", True
    eq.Add2 -1, Chr(34) & "GearModule" & Chr(34) & " = 1.5mm", True
    eq.Add2 -1, Chr(34) & "PressureAngle" & Chr(34) & " = 20deg", True
    eq.Add2 -1, Chr(34) & "GearCenterDistance" & Chr(34) & " = 90mm", True
    eq.Add2 -1, Chr(34) & "GearWidth" & Chr(34) & " = 18mm", True
    eq.Add2 -1, Chr(34) & "OutputShaftDiameter" & Chr(34) & " = 20mm", True
    eq.Add2 -1, Chr(34) & "BearingOD" & Chr(34) & " = 47mm", True
    eq.Add2 -1, Chr(34) & "BearingWidth" & Chr(34) & " = 14mm", True
    eq.Add2 -1, Chr(34) & "PlateThickness" & Chr(34) & " = 6mm", True
    eq.Add2 -1, Chr(34) & "ValveRotation" & Chr(34) & " = 90deg", True
    On Error GoTo 0
End Sub

Function NewPart()
    Dim model
    Set model = swApp.NewDocument(partTemplate, 0, 0, 0)
    If model Is Nothing Then Err.Raise vbObjectError + 102, , "Could not create part document"
    Set NewPart = model
End Function

Sub BeginTopSketch(ByRef model)
    Dim ok
    model.ClearSelection2 True
    ok = model.Extension.SelectByID2("Top Plane", "PLANE", 0, 0, 0, False, 0, Nothing, 0)
    If Not ok Then Err.Raise vbObjectError + 103, , "Could not select Top Plane"
    model.SketchManager.InsertSketch True
End Sub

Function ExtrudeSketch(ByRef model, ByVal depthMm, ByVal featureName)
    Dim feat
    model.SketchManager.InsertSketch True
    Set feat = model.FeatureManager.FeatureExtrusion2(True, False, False, 0, 0, M(depthMm), 0, False, False, False, False, 0, 0, False, False, False, False, True, True, True, 0, 0, False)
    If feat Is Nothing Then Err.Raise vbObjectError + 104, , "Extrusion failed: " & featureName
    feat.Name = featureName
    model.ClearSelection2 True
    Set ExtrudeSketch = feat
End Function

Function CutSketch(ByRef model, ByVal depthMm, ByVal featureName)
    Dim feat
    model.SketchManager.InsertSketch True
    Set feat = model.FeatureManager.FeatureCut3(True, False, False, 0, 0, M(depthMm), 0, False, False, False, False, 0, 0, False, False, False, False, False, True, True, False, False, False, 0, 0, False)
    If feat Is Nothing Then Err.Raise vbObjectError + 112, , "Cut failed: " & featureName
    feat.Name = featureName
    model.ClearSelection2 True
    Set CutSketch = feat
End Function

Sub SketchRectangle(ByRef model, ByVal widthMm, ByVal heightMm)
    model.SketchManager.CreateCornerRectangle -M(widthMm) / 2, -M(heightMm) / 2, 0, M(widthMm) / 2, M(heightMm) / 2, 0
End Sub

Sub SketchCircle(ByRef model, ByVal xMm, ByVal yMm, ByVal diaMm)
    model.SketchManager.CreateCircleByRadius M(xMm), M(yMm), 0, M(diaMm) / 2
End Sub

Sub SketchInnerRectangle(ByRef model, ByVal cxMm, ByVal cyMm, ByVal widthMm, ByVal heightMm)
    model.SketchManager.CreateCornerRectangle M(cxMm - widthMm / 2), M(cyMm - heightMm / 2), 0, M(cxMm + widthMm / 2), M(cyMm + heightMm / 2), 0
End Sub

Sub AddHoleArray(ByRef model, ByVal holes)
    Dim h
    For Each h In holes
        SketchCircle model, h(0), h(1), h(2)
    Next
End Sub

Sub SavePart(ByRef model, ByVal baseName, ByVal exportStl)
    Dim saveResult, nativePath, stepPath, stlPath
    nativePath = fso.BuildPath(nativeDir, baseName & ".SLDPRT")
    stepPath = fso.BuildPath(stepDir, baseName & ".STEP")
    saveResult = model.SaveAs3(nativePath, swSaveAsCurrentVersion, swSaveAsOptions_Silent)
    If saveResult <> 0 Then Err.Raise vbObjectError + 105, , "Native save failed: " & baseName & " errors=" & saveResult
    model.ForceRebuild3 False
    saveResult = model.SaveAs3(stepPath, swSaveAsCurrentVersion, swSaveAsOptions_Silent)
    If saveResult <> 0 Then Err.Raise vbObjectError + 106, , "STEP save failed: " & baseName & " errors=" & saveResult
    If exportStl Then
        stlPath = fso.BuildPath(stlDir, baseName & ".STL")
        saveResult = model.SaveAs3(stlPath, swSaveAsCurrentVersion, swSaveAsOptions_Silent)
        If saveResult <> 0 Then Err.Raise vbObjectError + 107, , "STL save failed: " & baseName & " errors=" & saveResult
    End If
    WScript.Echo "  OK " & baseName
    swApp.CloseDoc model.GetTitle
End Sub

Sub CreatePlate(ByVal name, ByVal widthMm, ByVal heightMm, ByVal thickMm, ByVal holes, ByVal material, ByVal finish, ByVal description, ByVal printed)
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, description, material, finish, printed
    BeginTopSketch model
    SketchRectangle model, widthMm, heightMm
    AddHoleArray model, holes
    ExtrudeSketch model, thickMm, "Plate_Extrude"
    SavePart model, name, printed
End Sub

Sub CreateCylinderPart(ByVal name, ByVal odMm, ByVal lengthMm, ByVal boreMm, ByVal material, ByVal finish, ByVal description, ByVal printed)
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, description, material, finish, printed
    BeginTopSketch model
    SketchCircle model, 0, 0, odMm
    If boreMm > 0 Then SketchCircle model, 0, 0, boreMm
    ExtrudeSketch model, lengthMm, "Revolved_Equivalent_Extrude"
    SavePart model, name, printed
End Sub

Sub CreateMotorBracket()
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "Adjustable 5840-31ZY motor cradle base; +/-5 mm mesh tuning", "6061-T6 aluminium", "Clear anodize", False
    BeginTopSketch model
    SketchRectangle model, 104, 86
    ' Supplier-tolerance slots: 12 mm long x 6.5 mm wide.
    SketchInnerRectangle model, -44, -30, 12, 6.5
    SketchInnerRectangle model, -44, 30, 12, 6.5
    SketchInnerRectangle model, 44, -30, 12, 6.5
    SketchInnerRectangle model, 44, 30, 12, 6.5
    SketchCircle model, 0, 0, 60
    ExtrudeSketch model, 6, "Motor_Cradle_Plate"
    SavePart model, "03_Motor_Bracket", False
End Sub

Sub ImportGearFromStep(ByVal name, ByVal teeth, ByVal material, ByVal finish)
    Dim model, path, ok, importData
    path = fso.BuildPath(stepDir, name & ".STEP")
    If Not fso.FileExists(path) Then Err.Raise vbObjectError + 113, , "Neutral gear STEP not found: " & path
    Set importData = swApp.GetImportFileData(path)
    ok = swApp.LoadFile3(path, "", importData)
    If ok Then Set model = swApp.ActiveDoc
    If model Is Nothing Then Err.Raise vbObjectError + 114, , "Could not open gear STEP: " & name
    AddGlobals model, 50
    AddReleaseProperties model, CStr(teeth) & " tooth module 1.5 gear; exact 20 degree involute STEP body; editable source in generate_neutral_cad.py", material, finish, True
    SavePart model, name, True
End Sub

Sub CreateGear(ByVal name, ByVal teeth, ByVal moduleMm, ByVal widthMm, ByVal boreMm, ByVal dBore, ByVal material, ByVal finish)
    Dim model, pa, rp, rb, ro, rr, pitchAng, backlash, invPa, alphaOuter, invOuter, halfOuter
    Dim k, theta, seg, firstX, firstY, lastX, lastY, x, y, a
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, CStr(teeth) & " tooth module " & CStr(moduleMm) & " gear; 20 degree pressure-angle involute chord profile", material, finish, True
    pa = 20 * PI / 180
    rp = moduleMm * teeth / 2
    rb = rp * Cos(pa)
    ro = rp + moduleMm
    rr = rp - 1.25 * moduleMm
    backlash = 0.3
    pitchAng = 2 * PI / teeth
    invPa = Tan(pa) - pa
    alphaOuter = Atn(Sqr((ro / rb) * (ro / rb) - 1))
    invOuter = Tan(alphaOuter) - alphaOuter
    halfOuter = PI / (2 * teeth) - backlash / (4 * rp) + invPa - invOuter

    ' Full, rebuild-stable outline. The flank endpoints are calculated from
    ' the 20 degree involute; straight chords are used between root and tip so
    ' the prototype sketch remains compact and reliably editable.
    BeginTopSketch model
    a = -pitchAng / 2
    firstX = M(rr * Cos(a)): firstY = M(rr * Sin(a))
    lastX = firstX: lastY = firstY
    For k = 0 To teeth - 1
        theta = 2 * PI * k / teeth
        a = theta - halfOuter
        x = M(ro * Cos(a)): y = M(ro * Sin(a))
        Set seg = model.SketchManager.CreateLine(lastX, lastY, 0, x, y, 0)
        lastX = x: lastY = y
        a = theta + halfOuter
        x = M(ro * Cos(a)): y = M(ro * Sin(a))
        Set seg = model.SketchManager.CreateLine(lastX, lastY, 0, x, y, 0)
        lastX = x: lastY = y
        If k = teeth - 1 Then
            x = firstX: y = firstY
        Else
            a = theta + pitchAng / 2
            x = M(rr * Cos(a)): y = M(rr * Sin(a))
        End If
        Set seg = model.SketchManager.CreateLine(lastX, lastY, 0, x, y, 0)
        lastX = x: lastY = y
    Next
    ExtrudeSketch model, widthMm, "Gear_Profile_Extrude"

    BeginTopSketch model
    If dBore Then
        SketchDProfile model, boreMm, 0.8
    Else
        SketchCircle model, 0, 0, boreMm
    End If
    CutSketch model, widthMm + 1, "Gear_Bore_Cut"
    SavePart model, name, True
End Sub

Sub SketchDProfile(ByRef model, ByVal diaMm, ByVal flatDepthMm)
    Dim n, i, a, r, x, y, px, py, firstX, firstY, seg, capX
    n = 40
    r = diaMm / 2
    capX = r - flatDepthMm
    For i = 0 To n - 1
        a = 2 * PI * i / n
        x = r * Cos(a)
        If x > capX Then x = capX
        y = r * Sin(a)
        If i = 0 Then
            firstX = M(x): firstY = M(y)
        Else
            Set seg = model.SketchManager.CreateLine(px, py, 0, M(x), M(y), 0)
        End If
        px = M(x): py = M(y)
    Next
    Set seg = model.SketchManager.CreateLine(px, py, 0, firstX, firstY, 0)
End Sub

Sub CreateStemAdapter()
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "Replaceable four-bolt valve-stem adapter; placeholder 14 x 10 mm socket", "17-4PH stainless or PA12-CF prototype", "Passivate if metal", True
    BeginTopSketch model
    SketchCircle model, 0, 0, 44
    SketchInnerRectangle model, 0, 0, 14.3, 10.3
    SketchCircle model, -15, 0, 6.6
    SketchCircle model, 15, 0, 6.6
    SketchCircle model, 0, -15, 6.6
    SketchCircle model, 0, 15, 6.6
    ExtrudeSketch model, 18, "Stem_Adapter_Flange"
    SavePart model, "10_Valve_Stem_Adapter", True
End Sub

Sub CreateOutputShaft()
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "20 mm output shaft with four-bolt lower flange; machine keyway and cross-pin per drawing", "4140 steel", "Zinc-nickel plate or 316 stainless alternative", False
    BeginTopSketch model
    SketchCircle model, 0, 0, 20
    ExtrudeSketch model, 161, "Output_Shaft_20mm"
    BeginTopSketch model
    SketchCircle model, 0, 0, 44
    SketchCircle model, -15, 0, 6.6
    SketchCircle model, 15, 0, 6.6
    SketchCircle model, 0, -15, 6.6
    SketchCircle model, 0, 15, 6.6
    ExtrudeSketch model, 10, "Output_Lower_Flange_With_Bolt_Holes"
    SavePart model, "06_Output_Shaft", False
End Sub

Sub CreateOutputGearHub()
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "Stepped keyed clutch hub with 50 mm pressure flange and 30 mm gear sleeve; machine 6 mm keyway", "C45 steel", "Zinc-nickel plate", False
    BeginTopSketch model
    SketchCircle model, 0, 0, 50
    SketchCircle model, 0, 0, 20.1
    ExtrudeSketch model, 6, "Clutch_Hub_Flange"
    BeginTopSketch model
    SketchCircle model, 0, 0, 30
    SketchCircle model, 0, 0, 20.1
    ExtrudeSketch model, 26, "Clutch_Hub_Sleeve"
    SavePart model, "07_Output_Gear_Hub", False
End Sub

Sub CreateBearingCarrier(ByVal name, ByVal description)
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, description & "; 47 H7 bearing seat with internal retainers", "6061-T6 aluminium", "Clear anodize; mask bearing bore", False
    BeginTopSketch model
    SketchCircle model, 0, 0, 76
    SketchCircle model, 0, 0, 47.02
    SketchCircle model, -30, 0, 6.6
    SketchCircle model, 30, 0, 6.6
    SketchCircle model, 0, -30, 6.6
    SketchCircle model, 0, 30, 6.6
    ExtrudeSketch model, 16, "Bearing_Carrier_Extrude"
    SavePart model, name, False
End Sub

Sub CreateClamp(ByVal name, ByVal valveNominal, ByVal neckOpening)
    Dim model
    Set model = NewPart()
    AddGlobals model, valveNominal
    AddReleaseProperties model, CStr(valveNominal) & " mm valve bridge clamp; " & CStr(neckOpening) & " mm HOLD opening", "6061-T6 aluminium", "Clear anodize with 3 mm EPDM liner", False
    BeginTopSketch model
    SketchRectangle model, 168, 70
    SketchInnerRectangle model, 0, 0, neckOpening, 46
    SketchCircle model, -70, -22, 8.5
    SketchCircle model, 70, -22, 8.5
    SketchCircle model, -70, 22, 8.5
    SketchCircle model, 70, 22, 8.5
    ExtrudeSketch model, 12, "Bridge_Clamp_Extrude"
    SavePart model, name, False
End Sub

Sub CreateCamRing()
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "Adjustable reed-switch magnetic cam ring", "PA12-CF", "As printed", True
    BeginTopSketch model
    SketchCircle model, 0, 0, 58
    SketchCircle model, 0, 0, 20.2
    SketchCircle model, 22, 0, 6.2
    SketchCircle model, 0, 22, 6.2
    ExtrudeSketch model, 10, "Reed_Cam_Extrude"
    SavePart model, "15_Reed_Switch_Cam", True
End Sub

Sub CreateSlottedBracket(ByVal name, ByVal widthMm, ByVal heightMm, ByVal thickMm, ByVal holeDia, ByVal description)
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, description, "316 stainless or PA12-CF prototype", "Deburr / passivate", False
    BeginTopSketch model
    SketchRectangle model, widthMm, heightMm
    SketchInnerRectangle model, -widthMm / 4, 0, 14, holeDia
    SketchInnerRectangle model, widthMm / 4, 0, 14, holeDia
    ExtrudeSketch model, thickMm, "Slotted_Bracket_Extrude"
    SavePart model, name, False
End Sub

Sub CreateTray(ByVal name, ByVal widthMm, ByVal heightMm, ByVal thickMm, ByVal holes, ByVal description)
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, description, "ASA or PA12-CF", "As printed", True
    BeginTopSketch model
    SketchRectangle model, widthMm, heightMm
    AddHoleArray model, holes
    ExtrudeSketch model, thickMm, "Tray_Base_Extrude"
    SavePart model, name, True
End Sub

Sub CreateHexPart(ByVal name, ByVal acrossFlatsMm, ByVal lengthMm, ByVal boreMm, ByVal material, ByVal description)
    Dim model, i, a, radius, x, y, px, py, firstX, firstY, seg
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, description, material, "Passivate", False
    BeginTopSketch model
    radius = acrossFlatsMm / (2 * Cos(PI / 6))
    For i = 0 To 6
        a = PI / 6 + 2 * PI * i / 6
        x = M(radius * Cos(a)): y = M(radius * Sin(a))
        If i = 0 Then
            firstX = x: firstY = y
        Else
            Set seg = model.SketchManager.CreateLine(px, py, 0, x, y, 0)
        End If
        px = x: py = y
    Next
    SketchCircle model, 0, 0, boreMm
    ExtrudeSketch model, lengthMm, "Manual_Hex_Extrude"
    SavePart model, name, False
End Sub

Sub CreateManualOverride()
    Dim model, radius, entities
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "30 mm external-hex manual override with 20.2 mm bore and cross-pin; 19 mm AF is impossible over a 20 mm shaft", "316 stainless steel", "Passivate", False
    BeginTopSketch model
    radius = 30 / (2 * Cos(PI / 6))
    entities = model.SketchManager.CreatePolygon(0, 0, 0, M(radius) * Cos(PI / 6), M(radius) * Sin(PI / 6), 0, 6, True)
    SketchCircle model, 0, 0, 20.2
    ExtrudeSketch model, 30, "Override_30mm_Hex_Hub"
    SavePart model, "24_Manual_Override", False
End Sub

Sub CreateEnclosureCover()
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "Gasketed gear/enclosure cover; split wet/electronics compartments", "ASA prototype; UV-stabilized PC/ASA production", "IP65/IP67 design intent", True
    BeginTopSketch model
    SketchRectangle model, 270, 210
    SketchInnerRectangle model, 0, 0, 256, 196
    ExtrudeSketch model, 170, "Cover_Walls"
    BeginTopSketch model
    SketchRectangle model, 270, 210
    ExtrudeSketch model, 6, "Cover_Roof"
    SavePart model, "25_Gear_Enclosure_Cover", True
End Sub

Sub CreateMotorReference()
    Dim model
    Set model = NewPart()
    AddGlobals model, 50
    AddReleaseProperties model, "5840-31ZY double-shaft gearmotor reference envelope - HOLD dimensions", "Purchased motor", "Purchased", False
    BeginTopSketch model
    SketchCircle model, 0, 0, 58
    ExtrudeSketch model, 72, "Motor_Envelope"
    SavePart model, "REF_5840_31ZY_Motor", False
End Sub

Sub CreateAssembly()
    Dim model, assy, cp, eq, saveResult, asmPath, stepPath
    Set model = swApp.NewDocument(assemblyTemplate, 0, 0, 0)
    If model Is Nothing Then Err.Raise vbObjectError + 108, , "Could not create assembly"
    Set assy = model
    AddGlobals model, 50
    AddReleaseProperties model, "Complete 50 mm quarter-turn irrigation valve actuator", "Mixed assembly", "Outdoor IP65/IP67 design intent", False

    asmPath = fso.BuildPath(nativeDir, "AMR_50mm_Valve_Actuator.SLDASM")
    stepPath = fso.BuildPath(stepDir, "AMR_50mm_Valve_Actuator.STEP")
    ' Establish a native assembly path before external references are inserted.
    saveResult = model.SaveAs3(asmPath, swSaveAsCurrentVersion, swSaveAsOptions_Silent)
    If saveResult <> 0 Then Err.Raise vbObjectError + 109, , "Initial assembly save failed errors=" & saveResult

    ' Exact design coordinates are inserted in one transaction. This avoids
    ' per-document activation prompts in installations with 3D Interconnect.
    AddAllComponents assy

    model.ForceRebuild3 False
    saveResult = model.SaveAs3(asmPath, swSaveAsCurrentVersion, swSaveAsOptions_Silent)
    If saveResult <> 0 Then Err.Raise vbObjectError + 109, , "Assembly save failed errors=" & saveResult
    saveResult = model.SaveAs3(stepPath, swSaveAsCurrentVersion, swSaveAsOptions_Silent)
    If saveResult <> 0 Then Err.Raise vbObjectError + 110, , "Assembly STEP save failed errors=" & saveResult
    WScript.Echo "  OK AMR_50mm_Valve_Actuator"
    swApp.CloseDoc model.GetTitle
End Sub

Sub AddAllComponents(ByRef assy)
    Dim items, i
    items = Array( _
        Array("01_Lower_Base_Plate", 0, 0, 0, "top"), Array("02_Upper_Support_Plate", 0, 0, 81, "top"), _
        Array("03_Motor_Bracket", -45, 0, 7, "top"), Array("04_Motor_Gear_40T", -45, 0, 29.5, "gear_motor"), _
        Array("05_Output_Gear_80T", 45, 0, 28, "gear_output"), Array("06_Output_Shaft", 45, 0, -34, "top"), _
        Array("07_Output_Gear_Hub", 45, 0, 22, "top"), Array("08_Clutch_Pressure_Plate", 45, 0, 48, "top"), _
        Array("09_Belleville_Stack_Reference", 45, 0, 53, "top"), Array("10_Valve_Stem_Adapter", 45, 0, -52, "top"), _
        Array("11_Valve_Clamp_50mm", 45, 0, -66, "top"), Array("13_AS5600_Bracket", 45, 0, 139, "top"), _
        Array("14_Magnet_Carrier", 45, 0, 127, "top"), Array("15_Reed_Switch_Cam", 45, 0, 87, "top"), _
        Array("16_Reed_Bracket_OPEN", 88, 38, 102, "top"), Array("17_Reed_Bracket_CLOSED", 88, -38, 102, "top"), _
        Array("18_Microswitch_Bracket_OPEN", 100, 58, 97, "top"), Array("19_Microswitch_Bracket_CLOSED", 100, -58, 97, "top"), _
        Array("20_Mechanical_Stop_Bracket", 45, -65, 87, "top"), Array("21_Battery_Tray", -75, 58, 106, "top"), _
        Array("22_TTGO_Tray", -58, -48, 106, "top"), Array("23_Motor_Driver_Tray", 68, 52, 106, "top"), _
        Array("24_Manual_Override", 45, 0, 97, "top"), Array("25_Gear_Enclosure_Cover", 0, 0, 170, "cover"), _
        Array("26_Lower_Bearing_Carrier", 45, 0, 6, "top"), Array("27_Upper_Bearing_Carrier", 45, 0, 65, "top"), _
        Array("REF_6204_2RS_Bearing", 45, 0, 7, "top"), Array("REF_6204_2RS_Bearing", 45, 0, 66, "top"), _
        Array("REF_5840_31ZY_Motor", -45, 0, -44, "top"), Array("REF_AS5600_PCB", 45, 0, 144, "top"), _
        Array("REF_TTGO_LORA32", -58, -48, 111, "top"), Array("REF_BTS7960_IBT2", 68, 52, 111, "top"), _
        Array("REF_18650_Cell", -107.7, 34, 121, "cell_x"), Array("REF_18650_Cell", -107.7, 58, 121, "cell_x"), _
        Array("REF_18650_Cell", -107.7, 82, 121, "cell_x"))
    For i = 0 To UBound(items)
        AddFixedComponent assy, items(i)(0), items(i)(1), items(i)(2), items(i)(3), items(i)(4)
    Next
End Sub

Sub AddFixedComponent(ByRef assy, ByVal baseName, ByVal xMm, ByVal yMm, ByVal zMm, ByVal orientation)
    Dim path, comp, partDoc, activeDoc, assemblyTitle, mathUtil, transform, data
    path = fso.BuildPath(nativeDir, baseName & ".SLDPRT")
    assemblyTitle = assy.GetTitle
    ' SOLIDWORKS requires an insertion document to be loaded before the older,
    ' configuration-independent AddComponent2 call can place it reliably.
    Set partDoc = swApp.OpenDoc(path, swDocPART)
    If partDoc Is Nothing Then Err.Raise vbObjectError + 115, , "Could not preload component: " & baseName
    Set activeDoc = swApp.ActivateDoc(assemblyTitle)
    ' Native SOLIDWORKS coordinates use Y as the Top-Plane normal.  Convert
    ' conventional assembly coordinates (X/Y horizontal, Z up) to X/Y/Z SW.
    Set comp = assy.AddComponent2(path, M(xMm), M(zMm), M(-yMm))
    If comp Is Nothing Then Err.Raise vbObjectError + 111, , "Could not add component: " & baseName
    If orientation <> "top" Then
        Set mathUtil = swApp.GetMathUtility
        If orientation = "cover" Then
            data = Array(CDbl(1), CDbl(0), CDbl(0), CDbl(0), CDbl(-1), CDbl(0), CDbl(0), CDbl(0), CDbl(-1), M(xMm), M(zMm), M(0 - yMm), CDbl(1), CDbl(0), CDbl(0), CDbl(0))
        ElseIf orientation = "cell_x" Then
            data = Array(CDbl(0), CDbl(1), CDbl(0), CDbl(-1), CDbl(0), CDbl(0), CDbl(0), CDbl(0), CDbl(1), M(xMm), M(zMm), M(0 - yMm), CDbl(1), CDbl(0), CDbl(0), CDbl(0))
        ElseIf orientation = "gear_motor" Then
            data = Array(CDbl(1), CDbl(0), CDbl(0), CDbl(0), CDbl(0), CDbl(1), CDbl(0), CDbl(-1), CDbl(0), M(xMm), M(zMm), M(0 - yMm), CDbl(1), CDbl(0), CDbl(0), CDbl(0))
        ElseIf orientation = "gear_output" Then
            data = Array(CDbl(0.999229036240723), CDbl(-0.0392598157590696), CDbl(0), CDbl(0), CDbl(0), CDbl(1), CDbl(-0.0392598157590696), CDbl(-0.999229036240723), CDbl(0), M(xMm), M(zMm), M(0 - yMm), CDbl(1), CDbl(0), CDbl(0), CDbl(0))
        Else
            Err.Raise vbObjectError + 116, , "Unknown component orientation: " & orientation
        End If
        Set transform = mathUtil.CreateTransform(data)
        If transform Is Nothing Then Err.Raise vbObjectError + 117, , "Could not create transform: " & baseName
        Set comp.Transform2 = transform
    End If
    assy.ClearSelection2 True
    comp.Select4 False, Nothing, False
    assy.FixComponent
    assy.ClearSelection2 True
End Sub

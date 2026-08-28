Option Explicit

Const swDocASSEMBLY = 2
Const swSaveAsCurrentVersion = 0
Const swSaveAsOptions_Silent = 1

Dim fso, root, sourcePath, targetPath, swApp, importData, ok, model, saveResult, components, count, eq, cp
Set fso = CreateObject("Scripting.FileSystemObject")
If WScript.Arguments.Count < 1 Then
    WScript.Echo "Usage: cscript //nologo import_solidworks_assembly.vbs PROJECT_ROOT"
    WScript.Quit 2
End If
root = fso.GetAbsolutePathName(WScript.Arguments(0))
sourcePath = fso.BuildPath(root, "hardware\actuator\cad\step\AMR_50mm_Valve_Actuator.STEP")
targetPath = fso.BuildPath(root, "hardware\actuator\cad\native\AMR_50mm_Valve_Actuator_RESOLVED.SLDASM")
If Not fso.FileExists(sourcePath) Then Err.Raise vbObjectError + 200, , "Assembly STEP missing: " & sourcePath

Set swApp = CreateObject("SldWorks.Application")
swApp.Visible = False
swApp.UserControl = False
Set importData = swApp.GetImportFileData(sourcePath)
ok = swApp.LoadFile3(sourcePath, "", importData)
If Not ok Then Err.Raise vbObjectError + 201, , "SOLIDWORKS STEP assembly import failed"
Set model = swApp.ActiveDoc
If model Is Nothing Then Err.Raise vbObjectError + 202, , "No imported assembly document"
If model.GetType <> swDocASSEMBLY Then
    Err.Raise vbObjectError + 203, , "STEP hierarchy imported as document type " & model.GetType & ", not an assembly"
End If

components = model.GetComponents(False)
If IsEmpty(components) Then
    count = 0
Else
    count = UBound(components) - LBound(components) + 1
End If
If count < 25 Then Err.Raise vbObjectError + 204, , "Imported assembly contains only " & count & " components"

Set eq = model.GetEquationMgr
eq.Add2 -1, Chr(34) & "ValveDiameter" & Chr(34) & " = 50mm", True
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
Set cp = model.Extension.CustomPropertyManager("")
cp.Add3 "Project", 30, "AMR 50/63 mm Irrigation Valve Actuator", 2
cp.Add3 "Revision", 30, "A-PROTOTYPE", 2
cp.Add3 "ReleaseStatus", 30, "PROTOTYPE - NOT RELEASED FOR PRODUCTION", 2
cp.Add3 "CriticalHold", 30, "MEASURE REAL MOTOR SHAFT/BODY AND VALVE STEM/NECK BEFORE RELEASE", 2

saveResult = model.SaveAs3(targetPath, swSaveAsCurrentVersion, swSaveAsOptions_Silent)
If saveResult <> 0 Then Err.Raise vbObjectError + 205, , "Native SLDASM save failed errors=" & saveResult
WScript.Echo "Imported native assembly with " & count & " resolved components"
swApp.CloseDoc model.GetTitle
swApp.ExitApp
Set swApp = Nothing

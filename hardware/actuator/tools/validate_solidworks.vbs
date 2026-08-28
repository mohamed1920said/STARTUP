Option Explicit

Const swDocPART = 1
Const swDocASSEMBLY = 2
Const swSolidBody = 0

Dim fso, root, nativeDir, swApp, folder, file, model, docType, bodies, components, partCount, assemblyCount
Set fso = CreateObject("Scripting.FileSystemObject")
If WScript.Arguments.Count < 1 Then WScript.Quit 2
root = fso.GetAbsolutePathName(WScript.Arguments(0))
nativeDir = fso.BuildPath(root, "hardware\actuator\cad\native")
Set swApp = CreateObject("SldWorks.Application")
swApp.Visible = False
swApp.UserControl = False
partCount = 0
assemblyCount = 0

Set folder = fso.GetFolder(nativeDir)
For Each file In folder.Files
    docType = 0
    If LCase(fso.GetExtensionName(file.Name)) = "sldprt" Then docType = swDocPART
    If LCase(fso.GetExtensionName(file.Name)) = "sldasm" Then docType = swDocASSEMBLY
    If docType <> 0 Then
        Set model = swApp.OpenDoc(file.Path, docType)
        If model Is Nothing Then Err.Raise vbObjectError + 300, , "Could not reopen " & file.Name
        model.ForceRebuild3 False
        If docType = swDocPART Then
            bodies = model.GetBodies2(swSolidBody, True)
            If IsEmpty(bodies) Then Err.Raise vbObjectError + 301, , "No solid body in " & file.Name
            partCount = partCount + 1
        Else
            components = model.GetComponents(False)
            If IsEmpty(components) Then Err.Raise vbObjectError + 302, , "No components in " & file.Name
            assemblyCount = UBound(components) - LBound(components) + 1
            If assemblyCount < 36 Then Err.Raise vbObjectError + 303, , "Assembly has only " & assemblyCount & " components"
        End If
        swApp.CloseDoc model.GetTitle
    End If
Next

WScript.Echo "Validated " & partCount & " native parts and one assembly with " & assemblyCount & " resolved components"
swApp.ExitApp
Set swApp = Nothing

param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'
$solidWorksApi = 'C:\Program Files\SOLIDWORKS Corp\SOLIDWORKS\api\redist'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$buildDir = Join-Path $ProjectRoot 'tmp\solidworks_motion'
$source = Join-Path $PSScriptRoot 'create_solidworks_motion.cs'
$encoder = Join-Path $PSScriptRoot 'encode_motion_frames.py'
$executable = Join-Path $buildDir 'create_solidworks_motion.exe'

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$interop = @(
    'SolidWorks.Interop.sldworks.dll',
    'SolidWorks.Interop.swconst.dll',
    'SolidWorks.Interop.swmotionstudy.dll'
)

$references = foreach ($name in $interop) {
    $path = Join-Path $solidWorksApi $name
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing SOLIDWORKS API library: $path" }
    Copy-Item -LiteralPath $path -Destination $buildDir -Force
    "/reference:$path"
}

& $compiler /nologo /platform:x64 /target:exe "/out:$executable" @references $source
if ($LASTEXITCODE -ne 0) { throw "C# compilation failed with exit code $LASTEXITCODE" }

& $executable $ProjectRoot
if ($LASTEXITCODE -ne 0) { throw "SOLIDWORKS Motion Study generation failed with exit code $LASTEXITCODE" }

& $executable $ProjectRoot export-only
if ($LASTEXITCODE -ne 0) { throw "SOLIDWORKS reopen validation or frame capture failed with exit code $LASTEXITCODE" }

& python $encoder $ProjectRoot
if ($LASTEXITCODE -ne 0) { throw "Motion preview encoding failed with exit code $LASTEXITCODE" }

& $executable $ProjectRoot validate-only
if ($LASTEXITCODE -ne 0) { throw "Final saved-assembly validation failed with exit code $LASTEXITCODE" }

Write-Host "Motion Study, validation report, frames, GIF, and poster generated successfully."

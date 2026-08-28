@echo off
set "PORT=%~1"
if not defined PORT set "PORT=COM4"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" set "PIO=pio"
echo === Flashing firmware to %PORT% ===
call "%PIO%" run -e central_gateway -t upload --upload-port "%PORT%"
if %errorlevel% neq 0 exit /b %errorlevel%
echo === Uploading filesystem to %PORT% ===
call "%PIO%" run -e central_gateway -t uploadfs --upload-port "%PORT%"
if %errorlevel% neq 0 exit /b %errorlevel%
echo === Done ===

@echo off
set PORT=COM4
echo === Flashing firmware to %PORT% ===
call "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" run -e central_gateway -t upload --upload-port %PORT%
if %errorlevel% neq 0 exit /b %errorlevel%
echo === Uploading filesystem to %PORT% ===
call "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" run -e central_gateway -t uploadfs --upload-port %PORT%
if %errorlevel% neq 0 exit /b %errorlevel%
echo === Done ===

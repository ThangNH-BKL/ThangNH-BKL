@echo off
setlocal
cd /d "%~dp0"
echo CameraFtpSync - Build desktop executable (Release x64)
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Configuration Release
if errorlevel 1 goto failed
if not exist "%~dp0build-vs\Release\CameraFtpSync.exe" goto missing
echo.
echo BUILD SUCCESSFUL
echo Executable: "%~dp0build-vs\Release\CameraFtpSync.exe"
echo The target computer also needs the Microsoft Visual C++ x64 Redistributable.
explorer.exe /select,"%~dp0build-vs\Release\CameraFtpSync.exe"
pause
exit /b 0
:missing
echo ERROR: Build returned success but the desktop executable was not found.
goto endfailure
:failed
echo.
echo BUILD FAILED. See the first error above and the newest text file in:
echo "%~dp0build-logs"
echo Required: VS2022 Desktop development with C++, CMake tools, MSVC v143,
echo Windows SDK, and C++ MFC for latest v143 build tools (x86 and x64).
:endfailure
pause
exit /b 1

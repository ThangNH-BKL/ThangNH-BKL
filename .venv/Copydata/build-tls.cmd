@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S CameraFtpSync -B build-ninja -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=D:/Project_Python/.venv/Copydata/build-ninja/tls-fix >build-logs/tls-configure.txt 2>&1
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-ninja --parallel 4 >build-logs/tls-build.txt 2>&1
exit /b %errorlevel%

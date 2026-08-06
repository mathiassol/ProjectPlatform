@echo off
setlocal
cd /d "%~dp0"

title ProjectPlatform Installer

if not exist "%~dp0pp.exe" (
    echo error: pp.exe not found next to this installer.
    echo Extract the full release zip before running Install.bat
    pause
    exit /b 1
)

echo.
echo  ProjectPlatform v1.3.0
echo  =====================
echo.

"%~dp0pp.exe" install
if errorlevel 1 (
    echo.
    echo Installation failed.
    pause
    exit /b 1
)

echo.
echo  Tip: open a NEW terminal and run:
echo    pp list
echo.
echo  Optional shell integration ^(off by default^):
echo    pp hook install
echo.
echo  Press any key to close...
pause >nul

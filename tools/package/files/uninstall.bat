@echo off
rem open-homm5 uninstaller for Windows: runs uninstall.ps1 (nothing else happens here).
rem This file must keep Windows (CRLF) line endings.
setlocal

if not exist "%~dp0uninstall.ps1" (
    echo uninstall.ps1 is not in the same folder as this file.
    echo Please extract the whole zip first - right-click the zip, then Extract All -
    echo and run uninstall.bat from the new folder.
    echo.
    pause
    exit /b 7
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %*
set "CODE=%ERRORLEVEL%"

rem Codes 1 and 2 mean PowerShell itself could not run the script, so the script
rem never got to wait for Enter. Wait here, so the message stays on screen.
if %CODE% GEQ 1 if %CODE% LEQ 2 (
    echo.
    echo The uninstaller could not start. Please tell us what the lines above say.
    pause
)
exit /b %CODE%

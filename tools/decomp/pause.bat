@echo off
rem Pause the decompilation agents: they save their work and stop within a few minutes.
echo paused > "%~dp0..\..\PAUSE"
echo Decompilation paused. Run resume.bat to let it continue.
pause

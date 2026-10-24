@echo off
rem Let the decompilation agents run again (at their next scheduled time).
del "%~dp0..\..\PAUSE" 2>nul
echo Decompilation resumed.
pause

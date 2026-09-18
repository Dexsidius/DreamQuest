@echo off
rem DreamQuest - builds the game. Double-click this, or run it from any prompt.
rem
rem It runs build.ps1 with the PowerShell execution policy bypassed for this one
rem command, because a fresh Windows refuses to run any .ps1 ("running scripts
rem is disabled on this system") and that is the wall most people hit first.
rem Arguments pass straight through: build.cmd -Run, build.cmd -Test.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
if errorlevel 1 (
    echo.
    echo The build failed. The messages above say why.
    pause
)

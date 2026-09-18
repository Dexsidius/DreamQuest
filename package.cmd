@echo off
rem DreamQuest - builds the game and packs it into a zip anyone can play from.
rem Double-click this. The zip lands in dist\.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
if errorlevel 1 (
    echo.
    echo Packaging failed. The messages above say why.
    pause
)

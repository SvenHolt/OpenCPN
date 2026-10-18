@echo off
rem Start seachart on Windows: double click for the menu, or pass the
rem usual seachart arguments, e.g.  seachart.bat harbours list
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0seachart.ps1" %*
if "%~1"=="" pause

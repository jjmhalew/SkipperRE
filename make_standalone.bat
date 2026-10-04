@echo off
rem make_standalone.bat [map] - een exe met alles erin (SkipperRE-standalone.exe): skipper.exe + JOUW spelbestanden uit
rem [map], extract\, data\ of %APPDATA%\SkipperRE\data (daar kopieert skipper.exe ze bij de eerste start heen). Omdat hij de
rem spelbestanden bevat is hij alleen voor jezelf: nooit delen of uploaden.
call "%~dp0build.bat" standalone %1
if errorlevel 1 pause

@echo off
rem make_android_bundle.bat [SkipperRE.apk] [cd-image] - een APK met alles erin (SkipperRE-bundle.apk): de Android-app +
rem JOUW image van de cd (SKIPPER_1.BIN, een .iso of een CloneCD-.img). Bij de eerste start pakt de app het image uit zonder
rem erom te vragen. Sleep de APK en het image op dit bestand, of zet ze ernaast (dan wordt de nieuwste SkipperRE-*.apk
rem genomen, anders die van android\, en een *.bin, *.iso of *.img).
rem Omdat hij de spelbestanden bevat is hij alleen voor je eigen apparaten: nooit delen of uploaden.
rem Hij is ondertekend met een eigen sleutel (eenmalig gemaakt, in %APPDATA%\SkipperRE\android-bundle.key): latere bundels
rem installeren als update ervan, maar niet over een SkipperRE van de Releases-pagina - verwijder die eerst (met zijn
rem opgeslagen spellen).
setlocal
cd /d "%~dp0"
set "APK="
set "IMG="
for %%a in (%*) do if /i "%%~xa"==".apk" (set "APK=%%~fa") else (set "IMG=%%~fa")
rem een .cue of .ccd: het image staat ernaast (.bin of .img)
if defined IMG for %%i in ("%IMG%") do (
    if /i "%%~xi"==".cue" set "IMG=%%~dpni.bin"
    if /i "%%~xi"==".ccd" set "IMG=%%~dpni.img"
)
if not defined APK for /f "delims=" %%f in ('dir /b /o:d SkipperRE-*.apk 2^>nul ^| findstr /v /i /l /x "SkipperRE-bundle.apk"') do set "APK=%CD%\%%f"
if not defined APK if exist "android\app\build\outputs\apk\release\app-release.apk" set "APK=%CD%\android\app\build\outputs\apk\release\app-release.apk"
if not defined IMG for %%f in (*.bin *.iso *.img) do set "IMG=%%~ff"
if not defined APK (
    echo Geen APK: sleep SkipperRE-^<versie^>.apk van de Releases-pagina op dit bestand, of zet hem ernaast.
    goto :fail
)
if not defined IMG (
    echo Geen image van de cd: sleep SKIPPER_1.BIN, een .iso of een .img op dit bestand, of zet het ernaast.
    goto :fail
)
if not exist "%IMG%" (
    echo %IMG% bestaat niet.
    goto :fail
)
call "%~dp0build.bat" apkbundle || goto :fail
echo APK:   %APK%
echo Image: %IMG%
out\apkbundle.exe "%APK%" "%IMG%" SkipperRE-bundle.apk game.img "%APPDATA%\SkipperRE\android-bundle.key" "SkipperRE bundle" || goto :fail
echo.
echo Klaar: SkipperRE-bundle.apk. Zet hem op je telefoon of tablet en installeer hem (USB-kabel, of adb install SkipperRE-bundle.apk).
echo Hij bevat de spelbestanden van jouw cd: houd hem voor jezelf, deel hem niet.
pause
exit /b 0

:fail
echo.
echo Geen bundel gemaakt.
pause
exit /b 1

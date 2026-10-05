@echo off
rem build.bat - bouwt SkipperRE op Windows 10/11; vooraf hoeft er niets geinstalleerd te zijn.
rem   build.bat                   out\skipper.exe, het spel (start hem, hij vraagt de eerste keer om de cd)
rem   build.bat dev               out\dev.exe, de ontwikkelbuild (log in de console, UBSan, PDB)
rem   build.bat standalone [map]  SkipperRE-standalone.exe: skipper.exe + JOUW spelbestanden in een exe, alleen voor
rem                               jezelf. De spelbestanden komen uit [map], extract\, data\ of %APPDATA%\SkipperRE\data.
rem De C-compiler is Zig (ziglang.org): een zig op PATH of "pip install ziglang" wordt gebruikt als die er is, anders
rem wordt de officiele Windows-versie eenmalig in tools\zig gedownload en tegen zijn SHA-256 gecontroleerd.
rem De bronlijst staat ook in build.sh (Git Bash en Linux).
setlocal
cd /d "%~dp0"

set "ZIG_VER=0.16.0"
set "ZIG_SHA=68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e"
set "ZIG_NAME=zig-x86_64-windows-%ZIG_VER%"
set "SRC=src\main.c src\host_win.c src\host_sdl.c src\plat_win.c src\plat_posix.c src\ini.c src\text_gdi.c src\text_ttf.c src\stb_impl.c src\dfile.c src\lingo.c src\builtins.c src\player.c src\stage.c src\xobj.c src\sound.c src\video.c src\trans.c src\disc.c src\pack.c src\dbgheap.c src\pad.c src\pad_sdl.c src\padinput.c src\texpack.c"
set "LIBS=-lgdi32 -luser32 -lwinmm -ldbghelp -lcomdlg32 -lsetupapi -lhid"
rem Zig bouwt anders voor de processor van de bouw-pc: een exe van een nieuwe pc of van de CI-runner gebruikt dan AVX2 /
rem AVX-512 en stopt op oudere processors met "illegal instruction" (0xc000001d). Gewone x86-64 draait overal.
set "CPU=-target x86_64-windows-gnu -mcpu=baseline"
set "FLAGS=-std=c99 %CPU% -g -fno-omit-frame-pointer -Wall -Wno-unused-function"

call :find_zig || goto :fail
if not exist out mkdir out

rem versie (res\skipperre.rc) en icoon: dat van de cd als de spelbestanden er zijn, anders res\skipperre.ico
set "ICON=res\skipperre.ico"
if exist "%APPDATA%\SkipperRE\data\Magnus.ico" set "ICON=%APPDATA%\SkipperRE\data\Magnus.ico"
if exist "extract\Magnus.ico" set "ICON=extract\Magnus.ico"
copy /y "%ICON%" out\skipper.ico >nul || goto :fail
copy /y res\skipperre.rc out\skipper.rc >nul || goto :fail
>>out\skipper.rc echo 1 ICON "skipper.ico"

if /i "%~1"=="dev" (
    echo Bouwen: out\dev.exe ^(ontwikkelbuild^) ...
    if exist out\dev.exe del out\dev.exe
    %ZIG% cc %FLAGS% -O1 -o out\dev.exe %SRC% out\skipper.rc %LIBS% || goto :fail
    echo Klaar: out\dev.exe
    exit /b 0
)

echo Bouwen: out\skipper.exe ...
if exist out\skipper.exe del out\skipper.exe
%ZIG% cc %FLAGS% -O2 -fno-sanitize=undefined -Wl,--subsystem,windows -o out\skipper.exe %SRC% out\skipper.rc %LIBS% || goto :fail
echo Klaar: out\skipper.exe
if /i not "%~1"=="standalone" exit /b 0

rem ---- standalone: skipper.exe + de spelbestanden (tools\pack.c kiest de map als er geen is opgegeven) ----
%ZIG% cc -std=c99 -O2 %CPU% -o out\pack.exe tools\pack.c || goto :fail
echo Spelbestanden inpakken ...
if "%~2"=="" (
    out\pack.exe out\skipper.exe || goto :nogame
) else (
    out\pack.exe out\skipper.exe "%~2" || goto :nogame
)
echo.
echo Klaar: SkipperRE-standalone.exe. Hij bevat de spelbestanden van jouw cd: houd hem voor jezelf, deel hem niet.
exit /b 0

:nogame
echo.
echo Geen standalone gemaakt. Start out\skipper.exe eerst een keer: die vindt de cd of vraagt om een image ervan, en
echo kopieert de spelbestanden naar %APPDATA%\SkipperRE\data. Of geef de map met Magnus.dxr op:
echo     make_standalone.bat D:\
goto :fail

:find_zig
if exist "tools\zig\%ZIG_NAME%\zig.exe" (set ZIG="%CD%\tools\zig\%ZIG_NAME%\zig.exe" & exit /b 0)
where zig >nul 2>nul && (set "ZIG=zig" & exit /b 0)
python -m ziglang version >nul 2>nul && (set "ZIG=python -m ziglang" & exit /b 0)
echo Zig %ZIG_VER% (de C-compiler, ongeveer 95 MB) is er nog niet: downloaden van ziglang.org naar tools\zig ...
if not exist tools\zig mkdir tools\zig
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -Uri 'https://ziglang.org/download/%ZIG_VER%/%ZIG_NAME%.zip' -OutFile 'tools\zig\zig.zip'; if ((Get-FileHash 'tools\zig\zig.zip' -Algorithm SHA256).Hash -ne '%ZIG_SHA%') { Remove-Item 'tools\zig\zig.zip'; throw 'SHA-256 van de download klopt niet' }" || exit /b 1
tar -xf tools\zig\zig.zip -C tools\zig || exit /b 1
del tools\zig\zig.zip
if not exist "tools\zig\%ZIG_NAME%\zig.exe" (echo In het Zig-archief zit geen %ZIG_NAME%\zig.exe & exit /b 1)
set ZIG="%CD%\tools\zig\%ZIG_NAME%\zig.exe"
exit /b 0

:fail
echo.
echo Bouwen MISLUKT.
exit /b 1

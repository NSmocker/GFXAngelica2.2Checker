@echo off
setlocal

set "ASSET_ROOT=B:\Git\PW146RUUnpackedAssets"
set "EXE=%~dp0bin\x64\Release\GFXAngelica2.2Checker.exe"
set "GFX_PATH=%~1"

if not exist "%EXE%" (
    echo Checker executable was not found:
    echo "%EXE%"
    echo Build the x64 Release configuration first.
    pause
    exit /b 1
)

if not defined GFX_PATH (
    set /p "GFX_PATH=Enter the full .gfx path (or drag a .gfx onto this .bat): "
)

if not defined GFX_PATH (
    echo No GFX file was specified.
    pause
    exit /b 2
)

"%EXE%" --interactive --root "%ASSET_ROOT%" --gfx "%GFX_PATH%"
set "RESULT=%ERRORLEVEL%"

if not "%RESULT%"=="0" echo Interactive preview failed with exit code %RESULT%.
pause
exit /b %RESULT%

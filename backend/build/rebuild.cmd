::```bat
:: filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/build/rebuild.cmd
@echo off
setlocal

REM Clean
mingw32-make clean
if errorlevel 1 exit /b 1

REM Build
mingw32-make -j
if errorlevel 1 exit /b 1

endlocal
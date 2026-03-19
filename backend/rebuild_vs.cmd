:: filepath: /e:/study/SoftwareLab/lab/se-metrics-platform/backend/rebuild_vs.cmd
@echo off
setlocal

REM 固定在 backend 目录执行
pushd "%~dp0"

set "SRC=%CD%"
set "B=%SRC%\build"
set "OPENSSL_ROOT=%SRC%\third_party\openssl"

if exist "%B%\CMakeCache.txt" (
  echo [INFO] Removing old build folder "%B%"...
  rmdir /s /q "%B%"
)

echo [INFO] Configure...
echo cmake -S "%SRC%" -B "%B%" -G "Visual Studio 18 2026" -A x64 -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT%"
cmake -S "%SRC%" -B "%B%" -G "Visual Studio 18 2026" -A x64 -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT%"
if errorlevel 1 goto :fail

echo [INFO] Build (Debug)...
cmake --build "%B%" --config Debug
if errorlevel 1 goto :fail

echo [OK] Done.
popd
exit /b 0

:fail
echo [ERROR] rebuild failed.
popd
exit /b 1
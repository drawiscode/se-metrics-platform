@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ============================================
echo   DevInsight - 软件工程一体化度量平台
echo ============================================
echo.

:: 检查配置文件
if not exist "config\config.env" (
    echo [提示] 未找到 config.env，正在从模板创建...
    if exist "config\config.env.example" (
        copy "config\config.env.example" "config\config.env" >nul
        echo [提示] 已创建 config\config.env，请编辑填入你的 API Key 后重新启动
        echo         必填项：GITHUB_TOKEN, LLM_API_KEY, EMBEDDING_API_KEY
        echo.
    ) else (
        echo [错误] 未找到 config\config.env.example 模板文件
        pause
        exit /b 1
    )
)

:: 检查 data 目录
if not exist "data" mkdir "data"
if not exist "data\log" mkdir "data\log"
if not exist "data\quality" mkdir "data\quality"
if not exist "data\repo_cache" mkdir "data\repo_cache"

:: 检查前端文件
if not exist "frontend-dist\index.html" (
    echo [警告] 未找到 frontend-dist\ 前端文件，将仅提供 API 服务
    echo        如需 Web 界面，请先执行: cd frontend ^&^& npm run build:release
    echo.
)

:: 检查可执行文件
if not exist "devinsight_backend.exe" (
    echo [错误] 未找到 devinsight_backend.exe，请确保可执行文件在正确位置
    pause
    exit /b 1
)

:: 检查依赖 DLL
set MISSING_DLL=0
for %%f in (sqlite3.dll libssl-3-x64.dll libcrypto-3-x64.dll) do (
    if not exist "%%f" (
        if %MISSING_DLL%==0 echo [警告] 缺少以下 DLL 文件:
        echo        %%f
        set MISSING_DLL=1
    )
)
if %MISSING_DLL%==1 (
    echo        请将以上 DLL 从 build 目录复制到 exe 同级目录
    echo.
)

:: Check external quality analysis tools (optional)
echo [CHECK] External quality analysis tools...
set TOOL_WARN=0

:: C/C++ 工具
for %%t in ("cppcheck:CPPCHECK_BIN" "clang-tidy:CLANG_TIDY_BIN" "cpplint:CPPLINT_BIN" "flawfinder:FLAWFINDER_BIN") do (
    for /f "tokens=1,2 delims=:" %%a in (%%t) do (
        call :CheckTool "%%a" "%%b" TOOL_WARN
    )
)

:: Python 工具 (pylint)
call :CheckTool "pylint" "PYLINT_BIN" TOOL_WARN

:: Java 工具 (checkstyle)
call :CheckTool "checkstyle" "CHECKSTYLE_BIN" TOOL_WARN

if %TOOL_WARN%==1 (
    echo.
    echo [WARNING] Some quality tools are not installed or configured.
    echo          Install missing tools and set paths in config\config.env.
    echo          See README.md optional dependencies section.
    echo.
)
goto :EndToolCheck

:CheckTool
setlocal
set TOOL_NAME=%~1
set ENV_NAME=%~2
:: Read tool path from config.env (KEY=VALUE format)
set TOOL_PATH=
if exist "config\config.env" (
    for /f "usebackq tokens=1,* delims==" %%A in ("config\config.env") do (
        if "%%A"=="%ENV_NAME%" set TOOL_PATH=%%B
    )
)
:: Skip if empty or matches default tool name
if "%TOOL_PATH%"=="" goto :EOF
if /i "%TOOL_PATH%"=="%TOOL_NAME%" goto :EOF
:: Check if contains path-like characters (drive letter : or separator \ /)
set HAS_PATH=0
echo %TOOL_PATH% | findstr /c:":" >nul && set HAS_PATH=1
echo %TOOL_PATH% | findstr /c:"\" >nul && set HAS_PATH=1
echo %TOOL_PATH% | findstr /c:"/" >nul && set HAS_PATH=1
if %HAS_PATH%==0 (
    :: No path-like chars, check if just a command name in PATH
    where %TOOL_NAME% >nul 2>&1
    if %errorlevel% neq 0 (
        echo [WARN] %TOOL_NAME% not found (config: %TOOL_PATH%), set %ENV_NAME% to full path in config.env
        endlocal & set %~3=1
    ) else (
        echo [ OK ] %TOOL_NAME% ready (PATH)
    )
) else (
    :: Full path provided, check if file exists
    if not exist "%TOOL_PATH%" (
        echo [WARN] %TOOL_NAME% path invalid: %TOOL_PATH%
        endlocal & set %~3=1
    ) else (
        echo [ OK ] %TOOL_NAME% ready
    )
)
goto :EOF

:EndToolCheck

echo [启动] Starting DevInsight backend...
echo.
echo   Frontend: http://localhost:8080
echo   API:      http://localhost:8080/api/
echo   Press Ctrl+C to stop
echo.
echo ============================================
echo.

devinsight_backend.exe

pause

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

:: 检查外部质量分析工具（可选，用于代码质量分析功能）
echo [检查] 外部质量分析工具...
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
    echo [提示] 部分质量分析工具未安装或未配置，相应语言的分析功能将不可用。
    echo        如需完整功能，请安装相应工具并在 config\config.env 中配置路径。
    echo        详见 README.md 可选依赖章节。
    echo.
)
goto :EndToolCheck

:CheckTool
setlocal
set TOOL_NAME=%~1
set ENV_NAME=%~2
:: 从 config.env 读取配置值（简单解析 KEY=VALUE）
set TOOL_PATH=
if exist "config\config.env" (
    for /f "usebackq tokens=1,* delims==" %%A in ("config\config.env") do (
        if "%%A"=="%ENV_NAME%" set TOOL_PATH=%%B
    )
)
:: 如果配置为空或默认值（没有具体路径），跳过检查
if "%TOOL_PATH%"=="" goto :EOF
if /i "%TOOL_PATH%"=="%TOOL_NAME%" goto :EOF
:: 检查是否包含路径特征（盘符 : 或分隔符 \ /）
set HAS_PATH=0
echo %TOOL_PATH% | findstr /c:":" >nul && set HAS_PATH=1
echo %TOOL_PATH% | findstr /c:"\" >nul && set HAS_PATH=1
echo %TOOL_PATH% | findstr /c:"/" >nul && set HAS_PATH=1
if %HAS_PATH%==0 (
    :: 不含路径特征，可能只是命令名（依赖 PATH），检查 PATH 中是否存在
    where %TOOL_NAME% >nul 2>&1
    if %errorlevel% neq 0 (
        echo [警告] %TOOL_NAME% 未找到（配置: %TOOL_PATH%），请在 config.env 中设置 %ENV_NAME% 为完整路径
        endlocal & set %~3=1
    ) else (
        echo [ OK ] %TOOL_NAME% 已就绪（PATH）
    )
) else (
    :: 是完整路径，检查文件是否存在
    if not exist "%TOOL_PATH%" (
        echo [警告] %TOOL_NAME% 路径无效: %TOOL_PATH%
        endlocal & set %~3=1
    ) else (
        echo [ OK ] %TOOL_NAME% 已就绪
    )
)
goto :EOF

:EndToolCheck

echo [启动] 正在启动 DevInsight 后端服务...
echo.
echo   前端页面: http://localhost:8080
echo   API 接口: http://localhost:8080/api/
echo   按 Ctrl+C 停止服务
echo.
echo ============================================
echo.

devinsight_backend.exe

pause

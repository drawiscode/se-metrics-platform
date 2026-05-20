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

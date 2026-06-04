#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo ""
echo "============================================"
echo "  DevInsight - 软件工程一体化度量平台"
echo "============================================"
echo ""

# 检查配置文件
if [ ! -f "config/config.env" ]; then
    if [ -f "config/config.env.example" ]; then
        cp "config/config.env.example" "config/config.env"
        echo "[提示] 已创建 config/config.env，请编辑填入你的 API Key 后重新启动"
        echo "       必填项：GITHUB_TOKEN, LLM_API_KEY, EMBEDDING_API_KEY"
        echo ""
        exit 1
    else
        echo "[错误] 未找到 config/config.env.example 模板文件"
        exit 1
    fi
fi

# 检查 data 目录
mkdir -p data/log data/quality data/repo_cache

# 检查前端文件
if [ ! -f "frontend-dist/index.html" ]; then
    echo "[警告] 未找到 frontend-dist/ 前端文件，将仅提供 API 服务"
    echo "       如需 Web 界面，请先执行: cd frontend && npm run build:release"
    echo ""
fi

# 检查可执行文件
if [ ! -f "devinsight_backend" ]; then
    echo "[错误] 未找到 devinsight_backend 可执行文件"
    exit 1
fi

# 检查外部质量分析工具（可选，用于代码质量分析功能）
echo "[检查] 外部质量分析工具..."

check_tool() {
    local TOOL_NAME="$1"
    local ENV_NAME="$2"
    local TOOL_PATH=""
    if [ -f "config/config.env" ]; then
        TOOL_PATH=$(grep "^${ENV_NAME}=" "config/config.env" 2>/dev/null | cut -d'=' -f2- | tr -d ' ')
    fi
    if [ -z "$TOOL_PATH" ] || [ "$TOOL_PATH" = "$TOOL_NAME" ]; then
        return 0
    fi
    # 含路径分隔符？检查文件是否存在
    if echo "$TOOL_PATH" | grep -q "/"; then
        if [ -x "$TOOL_PATH" ] || [ -f "$TOOL_PATH" ]; then
            echo " [ OK ] $TOOL_NAME 已就绪"
        else
            echo " [警告] $TOOL_NAME 路径无效: $TOOL_PATH"
            return 1
        fi
    else
        if command -v "$TOOL_NAME" >/dev/null 2>&1; then
            echo " [ OK ] $TOOL_NAME 已就绪（PATH）"
        else
            echo " [警告] $TOOL_NAME 未找到（配置: $TOOL_PATH），请在 config.env 中设置 ${ENV_NAME} 为完整路径"
            return 1
        fi
    fi
}

TOOL_WARN=0
for pair in "cppcheck:CPPCHECK_BIN" "clang-tidy:CLANG_TIDY_BIN" "cpplint:CPPLINT_BIN" "flawfinder:FLAWFINDER_BIN" "pylint:PYLINT_BIN" "checkstyle:CHECKSTYLE_BIN"; do
    tname="${pair%%:*}"
    ename="${pair##*:}"
    check_tool "$tname" "$ename" || TOOL_WARN=1
done

if [ "$TOOL_WARN" = "1" ]; then
    echo ""
    echo "[提示] 部分质量分析工具未安装或未配置，相应语言的分析功能将不可用。"
    echo "       如需完整功能，请安装相应工具并在 config/config.env 中配置路径。"
    echo "       详见 README.md 可选依赖章节。"
    echo ""
fi

echo "[启动] 正在启动 DevInsight 后端服务..."
echo ""
echo "  前端页面: http://localhost:8080"
echo "  API 接口: http://localhost:8080/api/"
echo "  按 Ctrl+C 停止服务"
echo ""
echo "============================================"
echo ""

./devinsight_backend

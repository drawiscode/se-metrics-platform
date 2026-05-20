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

echo "[启动] 正在启动 DevInsight 后端服务..."
echo ""
echo "  前端页面: http://localhost:8080"
echo "  API 接口: http://localhost:8080/api/"
echo "  按 Ctrl+C 停止服务"
echo ""
echo "============================================"
echo ""

./devinsight_backend

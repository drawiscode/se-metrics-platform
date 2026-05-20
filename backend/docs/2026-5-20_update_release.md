# 更新记录 — 2026-05-20：便携式发布与 CI/CD 自动化

## 概要
完成项目的便携式发布改造，实现"用户下载 zip → 解压 → 双击 start.bat → 浏览器打开即用"的交付体验。同时搭建 GitHub Actions CI/CD 自动构建流水线，后续只需推送 tag 即可自动生成发布包。修复了开发环境下终端中文乱码问题。

## 完成的功能

### 1. 便携式发布包
- 用户无需安装任何依赖（vcpkg / Node.js / CMake 等）
- 发布包仅包含：exe + dll + 前端静态文件 + 配置模板 + 启动脚本
- `start.bat` 一键启动，自动检查配置、创建目录、提示缺项
- 配置文件模板化（`config.env.example`），敏感信息用占位符

### 2. CI/CD 自动构建（GitHub Actions）
- 推送 `v*` 格式 tag 自动触发构建
- 云端编译后端（vcpkg + CMake）和前端（npm + Vite）
- 自动打包成 zip 并发布到 GitHub Releases
- 已配置 vcpkg 包缓存，后续构建显著加速

### 3. 开发体验修复
- 后端启动时自动设置终端为 UTF-8 编码，解决中文乱码
- 修复 `CMakeLists.txt` 中硬编码 vcpkg 路径，改为条件设置，兼容 CI 环境
- 后端自动探测多个数据库路径，兼容从 `build/` 目录启动的开发场景
- 后端支持托管前端静态文件 + SPA history 路由回退

## 新增文件清单

| 文件 | 说明 |
|------|------|
| `.github/workflows/release.yml` | GitHub Actions CI/CD 工作流配置 |
| `start.bat` | Windows 一键启动脚本 |
| `start.sh` | Linux/Mac 一键启动脚本 |
| `backend/vcpkg.json` | vcpkg 依赖清单，供 CI 自动安装 |
| `backend/config/config.env.example` | 重写配置模板（相对路径 + 中文说明） |

## 修改文件清单

| 文件 | 说明 |
|------|------|
| `backend/CMakeLists.txt` | vcpkg toolchain 改为条件设置，支持 CI 覆盖 |
| `backend/src/app/main.cpp` | 新增：静态文件托管 + SPA 回退 + UTF-8 控制台 + 多路径探测 |
| `frontend/vite.config.js` | 新增 `base: '/'` + build 输出路径 |
| `frontend/package.json` | 新增 `build:release` 脚本 |
| `.gitignore` | 新增 `frontend-dist/`、`*.exe`、`*.dll`、`*.ilk`、`*.pdb` |
| `README.md` | 新增快速开始、发布流程、开发环境章节 |

---

## 🚀 发布流程（组员操作指南）

### 日常开发（不影响发布）
```bash
git add .
git commit -m "描述你的改动"
git push
# 源码更新到 GitHub，不会生成 Release
```

### 准备发布新版本
```bash
# 1. 确保代码已提交推送
git add .
git commit -m "描述你的改动"
git push

# 2. 打 tag 并推送（格式必须为 v 开头，如 v1.0.1）
git tag v1.0.1
git push origin v1.0.1
```

推送 tag 后，GitHub Actions 自动执行：
1. 云端编译 C++ 后端（约 3 分钟）
2. 构建前端静态文件（约 1 分钟）
3. 安装 vcpkg 依赖（首次约 10 分钟，缓存后约 10 秒）
4. 打包所有文件成 zip
5. 发布到 https://github.com/drawiscode/se-metrics-platform/releases

### 发布前检查清单
- [ ] 代码已提交并推送到 `main` 分支
- [ ] 本地测试通过（后端编译 + 前端构建 + 启动验证）
- [ ] `config.env.example` 已包含所有新配置项
- [ ] 无调试代码或临时文件

### 查看构建状态
打开 https://github.com/drawiscode/se-metrics-platform/actions ，绿色 ✅ 表示成功，红色 ❌ 表示失败（点击查看日志）。

### 回滚发布
```bash
# 删除远程 tag
git push origin :refs/tags/v1.0.1
# 删除本地 tag
git tag -d v1.0.1
# 在 Releases 页面手动删除对应 Release
```

---

## 发布包结构（用户视角）

```
se-metrics-platform/
├── devinsight_backend.exe    # 后端可执行文件
├── sqlite3.dll               # SQLite 运行时库
├── libssl-3-x64.dll          # OpenSSL 运行时库
├── libcrypto-3-x64.dll       # OpenSSL 加密库
├── frontend-dist/            # 前端静态文件（HTML/JS/CSS）
├── config/
│   └── config.env.example    # 配置模板（用户复制为 config.env）
├── data/                     # 运行时数据目录（首次启动自动创建）
├── start.bat                 # Windows 一键启动
├── start.sh                  # Linux/Mac 一键启动
└── README.md                 # 使用说明
```

### 用户使用步骤
1. 解压 zip
2. 复制 `config/config.env.example` 为 `config/config.env`
3. 编辑 `config/config.env`，填入 GitHub Token 和 LLM API Key
4. 双击 `start.bat`（Windows）或运行 `./start.sh`（Linux/Mac）
5. 浏览器打开 `http://localhost:8080`

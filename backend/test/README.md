# DevInsight 测试套件

## 目录结构

```text
backend/test/
├── scripts/
│   ├── test_api_integration.ps1    # API 集成测试主脚本（57+ 断言）
│   └── run_tests.ps1               # 测试运行器（自动启动后端 → 测试 → 关闭）
├── data/
│   └── test_results.json           # 测试结果（自动生成）
├── routes_test.txt                 # 手动测试参考命令
└── README.md                       # 本文件
```

## 快速运行

```powershell
cd backend/test/scripts
.\run_tests.ps1
```

该命令将：
1. 自动启动后端（使用测试专用端口 18080 和独立数据库）
2. 等待服务就绪
3. 运行所有 API 集成测试
4. 关闭后端
5. 输出测试结果

## 单独运行（后端已在运行）

```powershell
cd backend/test/scripts
.\test_api_integration.ps1 -BaseUrl "http://127.0.0.1:8080" -Verbose
```

## 测试覆盖

| 模块 | 套件函数 | 断言数 |
|------|----------|:------:|
| 健康检查 | Test-HealthCheck | 1 |
| 仓库管理 | Test-RepoCRUD | 8 |
| 数据同步 | Test-RepoSync | 3 |
| 数据查询 | Test-DataQueries | 5 |
| 度量与健康 | Test-MetricsHealth | 6 |
| 热点与活动 | Test-HotspotsActivity | 3 |
| 知识库与 AI | Test-KnowledgeAI | 5 |
| CI 健康 | Test-CIHealth | 4 |
| 专家与周报 | Test-ExpertReport | 5 |
| 风险检测 | Test-RiskDetection | 3 |
| 系统日志 | Test-SystemLogs | 4 |
| 错误处理 | Test-ErrorHandling | 2 |
| 代码索引 | Test-CodeIndex | 1 |
| 质量分析 | Test-QualityAnalysis | 6 |
| 代码树 | Test-TreeEndpoint | 1 |
| 清理 | Test-DeleteCleanup | 2 |

**总计**: 16 个套件，57+ 断言

# 2.4 模块今日修改日志（2026-04-09, rty）

## 一、AI 问答能力增强（可不传 **repo_id**）

- **AI 问答接口**调整为仅强制要求 **question**，**repo_id** 变为可选。
- 当未传 **repo_id** 时，问答进入**全局知识库模式**，可跨仓库检索并生成回答。
- 当传入 **repo_id** 时，保持原有**单仓库聚焦问答**能力不变。

## 二、仓库清单类问题直答（本地数据库优先）

- 新增**仓库清单问题识别能力**（中英文关键词）。
- 对“有几个仓库、哪些仓库可回答”等问题，优先走**本地数据库直答**，不再依赖大模型推断。
- 直答内容包含以下关键信息：
- **仓库总数**
- **已构建知识库仓库数**
- **知识块总数**
- 每个仓库的 **repo_id**、仓库名与知识块数量

## 三、全局上下文与证据可追溯性增强

- 全局模式下自动注入**仓库概览信息**（总仓库数、已建库数、知识块总量、逐仓库统计）。
- 证据展示新增 **repo_id 来源标记**，跨仓库回答时可明确证据归属。
- AI 返回结构中的 **evidence** 项新增 **repo_id** 字段，便于前端做分仓展示与过滤。

## 四、知识库检索逻辑升级

- 检索函数支持 **repo_id <= 0** 时检索所有仓库。
- **SQL 绑定参数**改为按模式动态绑定，兼容单仓与全局两种路径。
- 保留原有**关键词评分排序策略**，维持结果稳定性。

## 五、编码与响应兼容性修复

- AI 路由统一返回 **UTF-8 JSON** 响应头，减少终端乱码问题。
- 问答序列化改为**标准 JSON 对象输出**，统一编码行为。
- 在请求大模型前加入 **UTF-8 容错清洗**，避免非法字节导致解析失败。

## 六、接口行为变更说明

- **POST /api/ai/ask**
- 旧行为：缺少 **repo_id** 或 **question** 会报错。
- 新行为：仅缺少 **question** 才报错；**repo_id** 可选。
- **GET /api/ai/conversations** 保持不变，仍支持 **repo_id** 与 **limit** 参数。

## 七、文档更新

- 更新 2.4 文档中的 **PowerShell 调用示例**。
- 补充**完整输出查看方式**，便于调试与验证。

## 八、涉及文件

- **ai_assistant.cpp**
- **ai_assistant.h**
- **knowledge_base.cpp**
- **knowledge_base.h**
- **routes_ai.cpp**

## 九、代码增删统计（本次 2.4 相关改动）

- 统计范围：`backend/src/ai/*`、`backend/src/api/routes_ai.cpp`、`backend/docs/CHANGELOG_2_4.md`
- **总计新增：334 行**
- **总计删除：89 行**

分文件明细：

- `backend/src/ai/ai_assistant.cpp`：+294 / -65
- `backend/src/ai/ai_assistant.h`：+3 / -1
- `backend/src/ai/knowledge_base.cpp`：+14 / -7
- `backend/src/ai/knowledge_base.h`：+2 / -1
- `backend/src/api/routes_ai.cpp`：+16 / -14
- `backend/docs/CHANGELOG_2_4.md`：+5 / -1

## 十、今日补充（2026-04-14, rty）

### 1) 2.4(3) 风险检测 MVP 已落地

- 新增风险检测核心模块：`backend/src/risk/detector.h`、`backend/src/risk/detector.cpp`。
- 新增风险路由：`backend/src/api/routes_risk.cpp`，并在主路由中注册。
- 新增风险数据库表：
- `risk_alert_runs`（扫描运行记录）
- `risk_alert_events`（告警事件明细）

### 2) 已实现的异常检测规则

- `code_churn_spike`：检测单日代码 churn 异常飙升。
- `issue_backlog_spike`：检测 open issues 积压突增。
- `pr_merge_latency_spike`：检测 PR 合并耗时异常上升。

### 3) 新增风险相关 API

- `POST /api/repos/{id}/risk/scan?days=30`：触发风险扫描。
- `GET /api/repos/{id}/risk/alerts`：查询告警（支持 status/severity/limit/offset）。
- `GET /api/repos/{id}/risk/alerts/summary?days=7`：查询告警摘要。

### 4) 与 AI/RAG 的联动

- 风险告警写入 `knowledge_chunks`（`source_type = risk_alert`），可被 AI 问答检索引用。

### 5) 同步稳定性与可诊断性增强

- 改进 `github_client.cpp`：
- GitHub 请求失败时返回更具体错误（含 `httplib` 错误码文本）。
- 增加连接/读写超时，降低卡死与假失败概率。
- 支持 `HTTPS_PROXY` / `HTTP_PROXY` 环境变量代理配置。

### 6) 编译与编码问题修复

- 修复 `risk/detector.cpp` 中字符串常量导致的 `C2001/C2146` 编译错误。
- 为 `detector.h`、`detector.cpp` 补充必要注释，便于后续规则扩展与维护。

## 十一、代码增删统计（今日全部改动）

- 统计范围：今日当前工作区全部改动（含新增文件）
- **总计新增：940 行**
- **总计删除：2 行**

分文件明细：

- `backend/CMakeLists.txt`：+2 / -0
- `backend/docs/update_2_4.md`：+62 / -1
- `backend/src/api/routes.cpp`：+2 / -0
- `backend/src/db/db.cpp`：+38 / -0
- `backend/src/repo_metrics/github_client.cpp`：+56 / -1
- `backend/src/api/routes_risk.cpp`：+92 / -0
- `backend/src/risk/detector.cpp`：+636 / -0
- `backend/src/risk/detector.h`：+52 / -0

## 十二、今日补充（2026-04-15, rty）

### 1) AI Prompt 时间锚点增强

- 在 `backend/src/ai/ai_assistant.cpp` 中注入服务端当前日期（`YYYY-MM-DD`）到系统提示词。
- 新增时间约束：要求模型以“当前系统日期 + 证据数据”为准，不再评论“未来/过去时间线是否合理”。
- 覆盖全局问答和单仓库问答两种 Prompt 模式。

### 2) 今日代码增删统计

- **总计新增：28 行**
- **总计删除：4 行**

分文件明细：

- `backend/src/ai/ai_assistant.cpp`：+28 / -4

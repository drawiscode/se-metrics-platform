# 2.4 模块今日修改日志（2026-04-09）

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
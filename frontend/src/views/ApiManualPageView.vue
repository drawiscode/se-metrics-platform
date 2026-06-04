<template>
  <section class="doc-page">
    <div class="breadcrumb">
      <span v-for="(item, idx) in page.breadcrumb" :key="`${item}-${idx}`">
        <span v-if="idx > 0" class="sep">&gt;</span>
        <span class="crumb">{{ item }}</span>
      </span>
    </div>

    <h1 class="page-title">{{ page.title }}</h1>

    <div v-for="(section, sIdx) in page.sections" :key="`${pageKey}-${sIdx}`" class="section">
      <h2 class="section-title">{{ section.heading }}</h2>

      <div v-for="(block, bIdx) in section.blocks" :key="`${pageKey}-${sIdx}-${bIdx}`">
        <p v-if="block.type === 'text'" class="paragraph">{{ block.text }}</p>

        <div v-if="block.type === 'note'" class="callout note">
          <span class="tag">✅ 说明</span>
          <div>{{ block.text }}</div>
        </div>

        <div v-if="block.type === 'warn'" class="callout warn">
          <span class="tag">⚠️ 注意</span>
          <div>{{ block.text }}</div>
        </div>

        <div v-if="block.type === 'error'" class="callout error">
          <span class="tag">⚠️ 错误</span>
          <div>{{ block.text }}</div>
        </div>

        <div v-if="block.type === 'table'" class="table-wrap">
          <table class="doc-table">
            <thead>
              <tr>
                <th>参数名</th>
                <th>类型</th>
                <th>说明</th>
                <th>是否必填</th>
                <th>默认值</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(row, rIdx) in block.rows" :key="`${pageKey}-${sIdx}-${bIdx}-${rIdx}`">
                <td>{{ row.name }}</td>
                <td>{{ row.type }}</td>
                <td>{{ row.desc }}</td>
                <td>{{ row.required }}</td>
                <td>{{ row.default }}</td>
              </tr>
            </tbody>
          </table>
        </div>

        <div v-if="block.type === 'code' || block.type === 'request' || block.type === 'response'" class="code-block">
          <div class="code-header" :class="block.type === 'response' ? 'resp-header' : (block.type === 'request' ? 'req-header' : '')">
            <span class="code-lang">{{ block.label || block.lang || 'text' }}</span>
            <button class="copy-btn" type="button" @click="copyCode(block.code, blockKey(pageKey, sIdx, bIdx))">
              {{ lastCopiedKey === blockKey(pageKey, sIdx, bIdx) ? '已复制' : '复制' }}
            </button>
          </div>
          <pre class="code-pre"><code>{{ block.code }}</code></pre>
        </div>
      </div>
    </div>
  </section>
</template>

<script>
  const pages = {
    'console/overview': {
      breadcrumb: ['首页', '接口文档', '控制台使用'],
      title: 'API 控制台使用说明',
      sections: [
        {
          heading: '功能说明',
          blocks: [
            {
              type: 'text',
              text: 'API 控制台用于直接发送 GET/POST/PUT/DELETE 请求，适合调试与快速验证接口输出。',
            },
            {
              type: 'note',
              text: '输入路径时请以 /api/ 开头，返回结果会格式化为 JSON。',
            },
          ],
        },
        {
          heading: '字段说明',
          blocks: [
            {
              type: 'table',
              rows: [
                { name: 'Method', type: 'string', desc: '请求方法（GET/POST/PUT/DELETE）', required: '是', default: 'GET' },
                { name: 'Path', type: 'string', desc: '接口路径', required: '是', default: '/api/' },
                { name: 'JSON Body', type: 'object', desc: '请求体，仅非 GET 生效', required: '否', default: '-' },
              ],
            },
          ],
        },
        {
          heading: '快速示例',
          blocks: [
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos?full_name=owner/repo',
            },
          ],
        },
        {
          heading: '注意事项',
          blocks: [
            {
              type: 'warn',
              text: 'POST/PUT 请求的 JSON 必须是合法 JSON 字符串，否则会提示解析失败。',
            },
          ],
        },
      ],
    },
    'repo/basic': {
      breadcrumb: ['首页', '接口文档', '仓库基础'],
      title: '仓库基础接口',
      sections: [
        {
          heading: '仓库列表与详情',
          blocks: [
            {
              type: 'text',
              text: '用于获取仓库列表、单仓库信息以及基础快照，适合首页列表与仓库详情页初始化。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos\nGET /api/repos/{repo_id}\nGET /api/repos/{repo_id}/snapshots',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {"id": 2, "full_name": "owner/repo2", "enabled": 1},\n    {"id": 1, "full_name": "owner/repo1", "enabled": 1}\n  ]\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "id": 1,\n  "full_name": "owner/repo",\n  "enabled": 1\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {\n      "id": 10,\n      "ts": "2026-05-01T12:00:00Z",\n      "stars": 10,\n      "forks": 2,\n      "open_issues": 3,\n      "watchers": 5,\n      "pushed_at": "2026-05-01T10:30:00Z"\n    }\n  ]\n}',
            },
          ],
        },
        {
          heading: '创建/更新/删除仓库',
          blocks: [
            {
              type: 'text',
              text: '创建仓库仅写入本地数据库，不会自动同步 GitHub 数据；需要单独调用同步接口。',
            },
            {
              type: 'table',
              rows: [
                { name: 'full_name', type: 'string', desc: '仓库全名 owner/repo', required: '是', default: '-' },
                { name: 'enabled', type: 'int', desc: '是否启用(0/1)', required: '否', default: '1' },
              ],
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos?full_name=owner/repo\nPUT /api/repos/{repo_id}?enabled=1\nDELETE /api/repos/{repo_id}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "ok": true,\n  "repo_id": 1,\n  "full_name": "owner/repo"\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "ok": true,\n  "id": 1,\n  "enabled": 1\n}',
            },
            {
              type: 'note',
              text: '删除仓库会清理本地缓存目录与相关数据。',
            },
          ],
        },
      ],
    },
    'repo/metrics': {
      breadcrumb: ['首页', '接口文档', '指标与健康'],
      title: '指标与健康接口',
      sections: [
        {
          heading: '核心指标',
          blocks: [
            {
              type: 'text',
              text: '用于展示仓库活跃度、健康分与综合评分，适合仓库概览页与仪表盘。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/metrics\nGET /api/repos/{repo_id}/health\nGET /api/repos/{repo_id}/score?tool=cppcheck',
            },
            {
              type: 'table',
              rows: [
                { name: 'tool', type: 'string', desc: '质量工具名称', required: '否', default: 'cppcheck' },
              ],
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "metrics": {\n    "commits_last_7d": 10,\n    "active_contributors_30d": 5,\n    "open_issues": 3\n  }\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "health": {\n    "score": 78.5,\n    "activity": 80,\n    "responsiveness": 70,\n    "quality": 85,\n    "release": 60\n  }\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "overall": 82.1,\n  "weights": {"health": 0.6, "quality": 0.4},\n  "health": {"score": 78.5},\n  "quality": {"score": 85.2},\n  "tool": "cppcheck"\n}',
            },
          ],
        },
      ],
    },
    'repo/activity-ci': {
      breadcrumb: ['首页', '接口文档', '活动与 CI'],
      title: '活动与 CI 接口',
      sections: [
        {
          heading: '活动与 CI 数据',
          blocks: [
            {
              type: 'text',
              text: '用于展示提交趋势、CI 运行历史与健康度。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/activity?days=30\nGET /api/repos/{repo_id}/ci/runs?limit=50\nGET /api/repos/{repo_id}/ci/health\nGET /api/repos/{repo_id}/ci/trend?days=14',
            },
            {
              type: 'table',
              rows: [
                { name: 'days', type: 'int', desc: '时间窗口(天)', required: '否', default: '30' },
                { name: 'limit', type: 'int', desc: '最大返回数量', required: '否', default: '50' },
                { name: 'offset', type: 'int', desc: '偏移量', required: '否', default: '0' },
                { name: 'status', type: 'string', desc: 'CI 状态过滤', required: '否', default: '-' },
                { name: 'conclusion', type: 'string', desc: 'CI 结论过滤', required: '否', default: '-' },
              ],
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {"date": "2026-05-01", "commits": 5}\n  ]\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {"run_id": 123, "name": "CI", "status": "completed", "conclusion": "success"}\n  ]\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "repo_id": 1,\n  "health_level": "warning",\n  "score": 76.5,\n  "failure_rate_24h": 0.2\n}',
            },
          ],
        },
      ],
    },
    'repo/intro': {
      breadcrumb: ['首页', '接口文档', '仓库介绍'],
      title: '仓库介绍接口',
      sections: [
        {
          heading: '接口说明',
          blocks: [
            {
              type: 'text',
              text: '用于展示仓库简介与更新时间，通常在仓库详情页展示。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/intro',
            },
            {
              type: 'note',
              text: '仓库介绍在首次同步成功后生成。',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "ok": true,\n  "repo_id": 1,\n  "intro_text": "仓库简介...",\n  "intro_updated_at": "2026-05-01 12:00:00"\n}',
            },
          ],
        },
      ],
    },
    'sync/overview': {
      breadcrumb: ['首页', '接口文档', '同步接口'],
      title: '同步接口',
      sections: [
        {
          heading: '全量/增量同步',
          blocks: [
            {
              type: 'text',
              text: '用于同步 GitHub 上的 issues/pulls/commits/releases，首次推荐使用 full。',
            },
            {
              type: 'table',
              rows: [
                { name: 'mode', type: 'string', desc: 'incremental 或 full', required: '否', default: 'incremental' },
                { name: 'issues_page_start', type: 'int', desc: 'issues 起始页', required: '否', default: '1' },
                { name: 'issues_pages_count', type: 'int', desc: 'issues 页数', required: '否', default: '1' },
                { name: 'pulls_page_start', type: 'int', desc: 'pulls 起始页', required: '否', default: '1' },
                { name: 'pulls_pages_count', type: 'int', desc: 'pulls 页数', required: '否', default: '1' },
                { name: 'commits_page_start', type: 'int', desc: 'commits 起始页', required: '否', default: '1' },
                { name: 'commits_pages_count', type: 'int', desc: 'commits 页数', required: '否', default: '1' },
                { name: 'releases_page_start', type: 'int', desc: 'releases 起始页', required: '否', default: '1' },
                { name: 'releases_pages_count', type: 'int', desc: 'releases 页数', required: '否', default: '1' },
              ],
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/sync?mode=incremental',
            },
            {
              type: 'code',
              label: '预期响应',
              code: '{\n  "ok": true,\n  "repo_id": 1,\n  "issues_upserted": 120,\n  "pulls_upserted": 20,\n  "commits_upserted": 200,\n  "releases_upserted": 2\n}',
            },
          ],
        },
        {
          heading: '提交文件同步',
          blocks: [
            {
              type: 'text',
              text: '用于生成热点文件/目录需要的 commit_files 数据。',
            },
            {
              type: 'table',
              rows: [
                { name: 'limit', type: 'int', desc: '单次处理 commits 数量', required: '否', default: '30' },
              ],
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/sync/commit_files?limit=30',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "ok": true,\n  "repo_id": 1,\n  "limit_commits": 30,\n  "total_files_processed": 2345\n}',
            },
          ],
        },
      ],
    },
    'hotspots/overview': {
      breadcrumb: ['首页', '接口文档', '热点分析'],
      title: '热点分析接口',
      sections: [
        {
          heading: '热点文件与目录',
          blocks: [
            {
              type: 'text',
              text: '基于 commit_files 统计热点文件/目录，可用于风险与代码治理。',
            },
            {
              type: 'table',
              rows: [
                { name: 'days', type: 'int', desc: '统计时间窗口(天)', required: '否', default: '0' },
                { name: 'top', type: 'int', desc: '返回数量', required: '否', default: '20' },
                { name: 'dir_depth', type: 'int', desc: '目录深度', required: '否', default: '2' },
              ],
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/hotfiles?days=30&top=20\nGET /api/repos/{repo_id}/hotdirs?days=30&top=10&dir_depth=2',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {"filename": "src/main.cpp", "commits": 12, "additions": 80, "deletions": 20}\n  ]\n}',
            },
          ],
        },
      ],
    },
    'experts/overview': {
      breadcrumb: ['首页', '接口文档', '隐形专家'],
      title: '隐形专家接口',
      sections: [
        {
          heading: '专家排名',
          blocks: [
            {
              type: 'text',
              text: '基于 PageRank 计算贡献者影响力，支持全局与模块内专家排行。',
            },
            {
              type: 'table',
              rows: [
                { name: 'top', type: 'int', desc: '返回数量', required: '否', default: '20' },
                { name: 'dir', type: 'string', desc: '模块路径', required: '否', default: '-' },
              ],
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/experts?top=20\nGET /api/repos/{repo_id}/experts/module?dir=src/ai&top=10',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "repo_id": 1,\n  "items": [\n    {"author": "alice", "score": 0.32}\n  ]\n}',
            },
          ],
        },
        {
          heading: '构建专家图谱',
          blocks: [
            {
              type: 'text',
              text: '当提交/PR 数据变更较大时建议手动重建。',
            },
            {
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/experts/build',
            },
          ],
        },
      ],
    },
    'quality/analysis': {
      breadcrumb: ['首页', '接口文档', '质量分析'],
      title: '质量分析接口',
      sections: [
        {
          heading: '启动分析与查询问题',
          blocks: [
            {
              type: 'text',
              text: '用于触发静态分析并查看问题列表、汇总与洞察结果。',
            },
            {
              type: 'table',
              rows: [
                { name: 'tool/tools', type: 'string', desc: '工具名称或列表', required: '否', default: 'cppcheck' },
                { name: 'ref', type: 'string', desc: '分支或提交', required: '否', default: 'main' },
                { name: 'mode', type: 'string', desc: 'full 或 incremental', required: '否', default: 'full' },
                { name: 'max_files', type: 'int', desc: '最大文件数', required: '否', default: '2000' },
                { name: 'path', type: 'string', desc: '可选：指定文件或目录', required: '否', default: '-' },
              ],
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/quality/analyze\n\nBody:\n{\n  "tool": "cppcheck",\n  "ref": "main"\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "ok": true,\n  "status": "Finished",\n  "run_id": 42,\n  "analyzed_files": 156,\n  "issues_new": 12,\n  "issues_fixed": 5\n}',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/quality/issues?status=active&limit=100\nGET /api/repos/{repo_id}/quality/summary?tool=cppcheck\nGET /api/repos/{repo_id}/quality/insights',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {\n      "id": 1,\n      "tool": "cppcheck",\n      "file_path": "src/main.cpp",\n      "line": 42,\n      "severity": "error",\n      "message": "Uninitialized variable: p"\n    }\n  ],\n  "limit": 100,\n  "offset": 0\n}',
            },
          ],
        },
      ],
    },
    'quality/tasks': {
      breadcrumb: ['首页', '接口文档', '质量任务与运行'],
      title: '质量任务与运行接口',
      sections: [
        {
          heading: '任务管理与运行',
          blocks: [
            {
              type: 'text',
              text: '用于创建可复用的分析任务，并按需运行。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/quality/tasks\nGET /api/repos/{repo_id}/quality/tasks?limit=50\nPOST /api/quality/tasks/{task_id}/run',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/quality/runs?limit=20\nGET /api/repos/{repo_id}/quality/trend?limit=10\nGET /api/repos/{repo_id}/quality/top?by=file&limit=20\nGET /api/repos/{repo_id}/quality/insights',
            },
            {
              type: 'code',
              lang: 'json',
              code: '{\n  "task_id": 12,\n  "repo_id": 1,\n  "status": "Pending",\n  "tools": "cppcheck",\n  "mode": "full"\n}',
            },
          ],
        },
      ],
    },
    'quality/baseline': {
      breadcrumb: ['首页', '接口文档', '质量基线与问题'],
      title: '质量基线与问题接口',
      sections: [
        {
          heading: '基线与问题状态',
          blocks: [
            {
              type: 'text',
              text: '用于设置质量门禁阈值，以及标记问题状态。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/quality/baseline\nPUT /api/repos/{repo_id}/quality/baseline\n\nBody:\n{\n  "min_score": 85\n}',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'PUT /api/quality/issues/{issue_id}\n\nBody:\n{\n  "status": "fixed"\n}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "configured": true,\n  "min_score": 85,\n  "max_new_issues": 0,\n  "max_error_issues": 0\n}',
            },
          ],
        },
        {
          heading: '删除接口',
          blocks: [
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'DELETE /api/quality/tasks/{task_id}\nDELETE /api/quality/runs/{run_id}\nDELETE /api/quality/issues/{issue_id}',
            },
            {
              type: 'warn',
              text: '删除操作不可恢复，请谨慎使用。',
            },
          ],
        },
      ],
    },
    'tasks/overview': {
      breadcrumb: ['首页', '接口文档', '任务清单'],
      title: '任务清单接口',
      sections: [
        {
          heading: '任务查询与创建',
          blocks: [
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/tasks?status=open&limit=200\nPOST /api/repos/{repo_id}/tasks\n\nBody:\n{\n  "title": "Add tests",\n  "priority": "P1",\n  "reason": "Coverage"\n}',
            },
          ],
        },
        {
          heading: '任务更新与删除',
          blocks: [
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'PATCH /api/tasks/{task_id}\n\nBody:\n{\n  "status": "done"\n}\n\nDELETE /api/tasks/{task_id}',
            },
          ],
        },
      ],
    },
    'reports/overview': {
      breadcrumb: ['首页', '接口文档', '周报接口'],
      title: '周报接口',
      sections: [
        {
          heading: '生成与查询',
          blocks: [
            {
              type: 'text',
              text: '生成周报通常耗时较长，建议异步提示用户等待。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/report/generate\nGET /api/repos/{repo_id}/reports?limit=10\nGET /api/repos/{repo_id}/reports/{report_id}\nGET /api/repos/{repo_id}/report/latest',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "id": 5,\n  "repo_id": 1,\n  "report_text": "本周提交 12 次...",\n  "created_at": "2026-05-01 12:00:00"\n}',
            },
          ],
        },
      ],
    },
    'ai/overview': {
      breadcrumb: ['首页', '接口文档', '知识库与问答'],
      title: '知识库与问答接口',
      sections: [
        {
          heading: '知识库构建与检索',
          blocks: [
            {
              type: 'text',
              text: '建议先构建知识库，再进行检索与问答。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/knowledge/build\nGET /api/repos/{repo_id}/knowledge/search?q=cache&top=10',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "ok": true,\n  "repo_id": 1,\n  "result": {\n    "issues_indexed": 10,\n    "pulls_indexed": 5,\n    "commits_indexed": 20,\n    "releases_indexed": 1,\n    "embeddings_generated": 100\n  }\n}',
            },
          ],
        },
        {
          heading: 'AI 问答与对话历史',
          blocks: [
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/ai/ask\n\nBody:\n{\n  "repo_id": 1,\n  "question": "What is the main architecture?"\n}\n\nGET /api/ai/conversations?repo_id=1&limit=20\nGET /api/ai/conversations/{id}',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "answer": "...",\n  "evidence": [\n    {"source_type": "issue", "source_id": "123", "title": "...", "snippet": "..."}\n  ],\n  "model": "deepseek-chat",\n  "success": true\n}',
              code: '{\n  "answer": "...",\n  "evidence": [\n    {"source_type": "issue", "source_id": "123", "title": "...", "snippet": "..."}\n  ],\n  "model": "deepseek-chat",\n  "success": true\n}',
            },
          ],
        },
      ],
    },
    'code/index': {
      breadcrumb: ['首页', '接口文档', '代码索引'],
      title: '代码索引接口',
      sections: [
        {
          heading: '索引构建',
          blocks: [
            {
              type: 'text',
              text: '构建代码索引用于语义检索与 AI 问答的代码片段支持。',
            },
            {
              type: 'table',
              rows: [
                { name: 'ref', type: 'string', desc: '分支或提交', required: '否', default: 'main' },
                { name: 'mode', type: 'string', desc: 'full 或 incremental', required: '否', default: 'full' },
                { name: 'max_files', type: 'int', desc: '最大文件数', required: '否', default: '2000' },
                { name: 'max_total_kb', type: 'int', desc: '最大文本大小(KB)', required: '否', default: '200000' },
              ],
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/code/index?ref=main&mode=full',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "ok": true,\n  "repo_id": 1,\n  "repo_head_sha": "abcdef",\n  "indexed_files": 120,\n  "indexed_chunks": 400,\n  "embeddings_generated": 400\n}',
            },
          ],
        },
      ],
    },
    'risk/overview': {
      breadcrumb: ['首页', '接口文档', '风险扫描'],
      title: '风险扫描接口',
      sections: [
        {
          heading: '风险扫描与告警',
          blocks: [
            {
              type: 'text',
              text: '扫描仓库风险事件并提供告警列表与摘要。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/risk/scan?days=30\nGET /api/repos/{repo_id}/risk/alerts?status=open&limit=50\nGET /api/repos/{repo_id}/risk/alerts/summary?days=7',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {"alert_type": "ci_pipeline_unhealthy", "severity": "warning", "status": "open"}\n  ]\n}',
            },
          ],
        },
      ],
    },
    'system/logs': {
      breadcrumb: ['首页', '接口文档', '系统日志'],
      title: '系统日志接口',
      sections: [
        {
          heading: '日志与统计',
          blocks: [
            {
              type: 'text',
              text: '用于查看系统操作日志与 AI 调用统计，便于运维排查。',
            },
            {
              type: 'code',
              label: '请求示例',
              lang: 'http',
              code: 'GET /api/system/logs/operations?limit=100\nGET /api/system/logs/ai-usage?repo_id=1&limit=50\nGET /api/system/logs/stats/today',
            },
            {
              type: 'code',
              label: '预期响应',
              lang: 'json',
              code: '{\n  "items": [\n    {"id": 1, "operation_type": "repo.sync", "status": "ok", "duration_ms": 1200}\n  ]\n}',
            },
          ],
        },
        {
          heading: '健康检查',
          blocks: [
            {
              type: 'code',
              lang: 'http',
              code: 'GET /api/health',
            },
          ],
        },
      ],
    },
  }

  export default {
    name: 'ApiManualPageView',
    data() {
      return {
        lastCopiedKey: '',
      }
    },
    computed: {
      pageKey() {
        return `${this.$route.params.section}/${this.$route.params.page}`
      },
      page() {
        return pages[this.pageKey] || {
          breadcrumb: ['首页', '文档', '未找到'],
          title: '文档不存在',
          sections: [
            {
              heading: '提示',
              blocks: [
                { type: 'error', text: '未找到对应的文档页面，请从左侧菜单选择有效章节。' },
              ],
            },
          ],
        }
      },
    },
    methods: {
      blockKey(pageKey, sectionIdx, blockIdx) {
        return `${pageKey}-${sectionIdx}-${blockIdx}`
      },
      async copyCode(text, key) {
        try {
          await navigator.clipboard.writeText(text)
          this.lastCopiedKey = key
          setTimeout(() => {
            if (this.lastCopiedKey === key) this.lastCopiedKey = ''
          }, 1500)
        } catch {
          this.lastCopiedKey = key
          setTimeout(() => {
            if (this.lastCopiedKey === key) this.lastCopiedKey = ''
          }, 1500)
        }
      },
    },
  }
</script>

<style scoped>
  .doc-page {
    padding: 8px 4px 24px;
  }

  .breadcrumb {
    font-size: 13px;
    color: #6b7280;
    margin-bottom: 8px;
  }

  .sep {
    margin: 0 6px;
  }

  .page-title {
    font-size: 28px;
    font-weight: 700;
    margin: 6px 0 16px;
    color: #0f172a;
  }

  .section {
    margin-bottom: 24px;
  }

  .section-title {
    font-size: 18px;
    font-weight: 700;
    margin: 0 0 10px;
    color: #111827;
  }

  .paragraph {
    margin: 0 0 10px;
    color: #374151;
    line-height: 1.7;
  }

  .callout {
    display: flex;
    gap: 10px;
    padding: 10px 12px;
    border-radius: 8px;
    margin: 10px 0;
    font-size: 14px;
  }

  .callout.note {
    background: #ecfeff;
    color: #0e7490;
  }

  .callout.warn {
    background: #fff7ed;
    color: #c2410c;
  }

  .callout.error {
    background: #fef2f2;
    color: #b91c1c;
  }

  .tag {
    font-weight: 700;
    white-space: nowrap;
  }

  .table-wrap {
    overflow: auto;
    margin: 10px 0 16px;
  }

  .doc-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 14px;
  }

  .doc-table th,
  .doc-table td {
    border: 1px solid #e5e7eb;
    padding: 8px 10px;
    text-align: left;
  }

  .doc-table th {
    background: #f8fafc;
    font-weight: 600;
  }

  .code-block {
    background: #ffffff;
    color: #2d2e30;
    border-radius: 10px;
    overflow: hidden;
    margin: 12px 0 18px;
  }

  .code-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 12px;
    background: #f0f9ff;
    font-size: 12px;
  }

  .req-header {
    background: #fef3c7;
  }

  .resp-header {
    background: #d1fae5;
  }

  .code-lang {
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .copy-btn {
    border: 1px solid #334155;
    background: #1F4F82;
    color: #e2e8f0;
    border-radius: 6px;
    padding: 2px 10px;
    font-size: 12px;
    cursor: pointer;
  }

  .copy-btn:hover {
    background: #0f172a;
  }

  .code-pre {
    margin: 0;
    padding: 12px;
    font-family: ui-monospace, Consolas, monospace;
    font-size: 13px;
    white-space: pre-wrap;
  }
</style>

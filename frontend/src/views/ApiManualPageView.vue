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

        <div v-if="block.type === 'code'" class="code-block">
          <div class="code-header">
            <span class="code-lang">{{ block.lang || 'text' }}</span>
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
              lang: 'http',
              code: 'GET /api/repos',
            },
            {
              type: 'code',
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
              text: '用于获取仓库列表、单仓库信息以及基础快照。',
            },
            {
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos\nGET /api/repos/{repo_id}\nGET /api/repos/{repo_id}/snapshots',
            },
          ],
        },
        {
          heading: '创建/更新/删除仓库',
          blocks: [
            {
              type: 'table',
              rows: [
                { name: 'full_name', type: 'string', desc: '仓库全名 owner/repo', required: '是', default: '-' },
                { name: 'enabled', type: 'int', desc: '是否启用(0/1)', required: '否', default: '1' },
              ],
            },
            {
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos?full_name=owner/repo\nPUT /api/repos/{repo_id}?enabled=1\nDELETE /api/repos/{repo_id}',
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
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/metrics\nGET /api/repos/{repo_id}/health\nGET /api/repos/{repo_id}/score?tool=cppcheck',
            },
            {
              type: 'table',
              rows: [
                { name: 'tool', type: 'string', desc: '质量工具名称', required: '否', default: 'cppcheck' },
              ],
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
              type: 'code',
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
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/intro',
            },
            {
              type: 'note',
              text: '仓库介绍在首次同步成功后生成。',
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
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/sync?mode=incremental',
            },
          ],
        },
        {
          heading: '提交文件同步',
          blocks: [
            {
              type: 'table',
              rows: [
                { name: 'limit', type: 'int', desc: '单次处理 commits 数量', required: '否', default: '30' },
              ],
            },
            {
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/sync/commit_files?limit=30',
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
              type: 'table',
              rows: [
                { name: 'days', type: 'int', desc: '统计时间窗口(天)', required: '否', default: '0' },
                { name: 'top', type: 'int', desc: '返回数量', required: '否', default: '20' },
                { name: 'dir_depth', type: 'int', desc: '目录深度', required: '否', default: '2' },
              ],
            },
            {
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/hotfiles?days=30&top=20\nGET /api/repos/{repo_id}/hotdirs?days=30&top=10&dir_depth=2',
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
              type: 'table',
              rows: [
                { name: 'top', type: 'int', desc: '返回数量', required: '否', default: '20' },
                { name: 'dir', type: 'string', desc: '模块路径', required: '否', default: '-' },
              ],
            },
            {
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/experts?top=20\nGET /api/repos/{repo_id}/experts/module?dir=src/ai&top=10',
            },
          ],
        },
        {
          heading: '构建专家图谱',
          blocks: [
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
              type: 'table',
              rows: [
                { name: 'tool/tools', type: 'string', desc: '工具名称或列表', required: '否', default: 'cppcheck' },
                { name: 'ref', type: 'string', desc: '分支或提交', required: '否', default: 'main' },
                { name: 'mode', type: 'string', desc: 'full 或 incremental', required: '否', default: 'full' },
                { name: 'max_files', type: 'int', desc: '最大文件数', required: '否', default: '2000' },
              ],
            },
            {
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/quality/analyze\n\nBody:\n{\n  "tool": "cppcheck",\n  "ref": "main"\n}',
            },
            {
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/quality/issues?status=active&limit=100\nGET /api/repos/{repo_id}/quality/summary?tool=cppcheck',
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
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/quality/tasks\nGET /api/repos/{repo_id}/quality/tasks?limit=50\nPOST /api/quality/tasks/{task_id}/run',
            },
            {
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/quality/runs?limit=20\nGET /api/repos/{repo_id}/quality/trend?limit=10\nGET /api/repos/{repo_id}/quality/top?by=file&limit=20',
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
              type: 'code',
              lang: 'http',
              code: 'GET /api/repos/{repo_id}/quality/baseline\nPUT /api/repos/{repo_id}/quality/baseline\n\nBody:\n{\n  "min_score": 85\n}',
            },
            {
              type: 'code',
              lang: 'http',
              code: 'PUT /api/quality/issues/{issue_id}\n\nBody:\n{\n  "status": "fixed"\n}',
            },
          ],
        },
        {
          heading: '删除接口',
          blocks: [
            {
              type: 'code',
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
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/report/generate\nGET /api/repos/{repo_id}/reports?limit=10\nGET /api/repos/{repo_id}/reports/{report_id}\nGET /api/repos/{repo_id}/report/latest',
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
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/knowledge/build\nGET /api/repos/{repo_id}/knowledge/search?q=cache&top=10',
            },
          ],
        },
        {
          heading: 'AI 问答与对话历史',
          blocks: [
            {
              type: 'code',
              lang: 'http',
              code: 'POST /api/ai/ask\n\nBody:\n{\n  "repo_id": 1,\n  "question": "What is the main architecture?"\n}\n\nGET /api/ai/conversations?repo_id=1&limit=20\nGET /api/ai/conversations/{id}',
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
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/code/index?ref=main&mode=full',
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
              type: 'code',
              lang: 'http',
              code: 'POST /api/repos/{repo_id}/risk/scan?days=30\nGET /api/repos/{repo_id}/risk/alerts?status=open&limit=50\nGET /api/repos/{repo_id}/risk/alerts/summary?days=7',
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
              type: 'code',
              lang: 'http',
              code: 'GET /api/system/logs/operations?limit=100\nGET /api/system/logs/ai-usage?repo_id=1&limit=50\nGET /api/system/logs/stats/today',
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
    background: #ECFEFF;
    font-size: 12px;
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

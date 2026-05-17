<template>
  <section>
    <div class="row">
      <h2>代码质量分析 — Repo #{{ repoId }}</h2>
      <RouterLink :to="`/repos/${repoId}`">← 返回仓库</RouterLink>
    </div>

    <p v-if="err" class="err">{{ err }}</p>

    <!-- ====== 质量总览卡片 ====== -->
    <div class="grid" v-if="!err">
      <div class="card">
        <h3>质量评分</h3>
        <div class="score-display">
          <div class="score-circle" :class="scoreLevelClass(summary.quality?.score ?? 0)">
            <span class="score-num">{{ formatScore(summary.quality?.score) }}</span>
            <span class="score-label">/ 100</span>
          </div>
          <div class="score-meta">
            <div>问题总数: <strong>{{ summary.quality?.total_issues ?? 0 }}</strong></div>
            <div>涉及文件: <strong>{{ summary.quality?.files_with_issues ?? 0 }}</strong></div>
            <div>每千行密度: <strong>{{ formatDensity(summary.density_per_kloc) }}</strong></div>
            <div v-if="summary.lines_analyzed">分析行数: <strong>{{ summary.lines_analyzed.toLocaleString() }}</strong></div>
          </div>
        </div>
        <div v-if="summary.baseline?.configured" class="baseline-info" :class="{ degraded: summary.baseline.degraded }">
          <span v-if="summary.baseline.degraded">⚠ {{ baselineStatusText }}</span>
          <span v-else>✓ 质量达标 — 基线 {{ summary.baseline.min_score }} 分</span>
        </div>
      </div>

      <div class="card">
        <h3>严重性分布</h3>
        <div class="severity-bars" v-if="severityEntries.length">
          <div v-for="[sev, count] in severityEntries" :key="sev" class="sev-row">
            <span class="sev-label" :class="'sev-' + sev.toLowerCase()">{{ sev }}</span>
            <div class="sev-bar-wrap">
              <div class="sev-bar" :class="'sev-' + sev.toLowerCase()"
                   :style="{ width: barWidth(count, maxSeverityCount) }"></div>
            </div>
            <span class="sev-count">{{ count }}</span>
          </div>
        </div>
        <p v-else class="muted">暂无问题数据</p>
      </div>
    </div>

    <!-- ====== 操作栏 ====== -->
    <div class="card card-wide action-bar">
      <div class="row row-wrap">
        <button :disabled="busy" @click="triggerAnalysis" class="btn-primary">🔍 运行分析</button>
        <select v-model="analysisTool">
          <option value="cppcheck">cppcheck</option>
          <option value="clang-tidy">clang-tidy</option>
          <option value="cpplint">cpplint</option>
          <option value="flawfinder">flawfinder</option>
          <option value="all">全部工具</option>
        </select>
        <select v-model="analysisMode">
          <option value="full">全量模式</option>
          <option value="hot">热点模式</option>
        </select>
        <label class="inline-label">
          文件上限 <input type="number" v-model.number="analysisMaxFiles" min="10" max="10000" style="width:80px" />
        </label>
        <span v-if="analyzeResult" class="muted">
          {{ analyzeResult.status === 'Finished' ? '✓' : '✗' }}
          文件 {{ analyzeResult.analyzed_files }} |
          行数 {{ (analyzeResult.lines_analyzed ?? 0).toLocaleString() }} |
          新增 {{ analyzeResult.issues_new ?? 0 }} |
          修复 {{ analyzeResult.issues_fixed ?? 0 }}
        </span>
        <span v-if="analyzeResult?.error" class="err">{{ analyzeResult.error }}</span>
      </div>
    </div>

    <!-- ====== 标签页切换 ====== -->
    <div class="tabs">
      <button :class="{ active: tab === 'issues' }" @click="tab = 'issues'; loadIssues()">问题列表</button>
      <button :class="{ active: tab === 'trend' }" @click="tab = 'trend'; loadTrend()">趋势图</button>
      <button :class="{ active: tab === 'top' }" @click="tab = 'top'; loadTopList()">Top 排行</button>
      <button :class="{ active: tab === 'tasks' }" @click="tab = 'tasks'; loadTasks()">分析任务</button>
      <button :class="{ active: tab === 'runs' }" @click="tab = 'runs'; loadRuns()">运行记录</button>
      <button :class="{ active: tab === 'baseline' }" @click="tab = 'baseline'; loadBaseline()">质量基线</button>
    </div>

    <!-- ====== 问题列表 ====== -->
    <div v-if="tab === 'issues'" class="card card-wide">
      <div class="row row-wrap">
        <select v-model="issueFilter.tool" @change="loadIssues()">
          <option value="">全部工具</option>
          <option value="cppcheck">cppcheck</option>
          <option value="clang-tidy">clang-tidy</option>
          <option value="cpplint">cpplint</option>
          <option value="flawfinder">flawfinder</option>
        </select>
        <select v-model="issueFilter.severity" @change="loadIssues()">
          <option value="">全部等级</option>
          <option value="error">error</option>
          <option value="warning">warning</option>
          <option value="style">style</option>
          <option value="performance">performance</option>
          <option value="portability">portability</option>
          <option value="information">information</option>
        </select>
        <select v-model="issueFilter.status" @change="loadIssues()">
          <option value="active">活跃问题</option>
          <option value="fixed">已修复</option>
          <option value="ignored">已忽略</option>
          <option value="false_positive">误报</option>
          <option value="all">全部状态</option>
        </select>
        <span class="muted">共 {{ issueTotal }} 条，当前 {{ issues.length }} 条</span>
      </div>

      <table class="tbl" v-if="issues.length">
        <thead>
          <tr>
            <th>文件</th>
            <th>行:列</th>
            <th>工具</th>
            <th>规则</th>
            <th>等级</th>
            <th>状态</th>
            <th>消息</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="iss in issues" :key="iss.id" :class="{ 'row-fixed': iss.status === 'fixed' }">
            <td class="file-cell" :title="iss.file_path">{{ shortenPath(iss.file_path) }}</td>
            <td>{{ iss.line }}:{{ iss.column }}</td>
            <td><span class="pill-sm">{{ iss.tool }}</span></td>
            <td><code>{{ iss.rule_id }}</code></td>
            <td><span class="badge" :class="'badge-' + (iss.severity || '').toLowerCase()">{{ iss.severity }}</span></td>
            <td><span class="pill-sm" :class="'status-' + iss.status">{{ statusLabel(iss.status) }}</span></td>
            <td class="msg-cell" :title="iss.message">{{ iss.message }}</td>
            <td class="actions-cell">
              <select v-if="iss.status !== 'fixed'" @change="updateIssueStatus(iss, $event.target.value)" class="action-select">
                <option value="">操作...</option>
                <option value="fixed">标记已修复</option>
                <option value="ignored">忽略</option>
                <option value="false_positive">标记误报</option>
              </select>
              <button v-if="iss.status === 'fixed'" @click="updateIssueStatus(iss, 'active')" class="btn-sm">恢复</button>
            </td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">暂无问题数据。</p>

      <div class="row row-center" v-if="issueTotal > pageSize">
        <button :disabled="issuePage <= 0" @click="issuePage--; loadIssues()">上一页</button>
        <span>第 {{ issuePage + 1 }} 页 / 共 {{ Math.ceil(issueTotal / pageSize) }} 页</span>
        <button :disabled="(issuePage + 1) * pageSize >= issueTotal" @click="issuePage++; loadIssues()">下一页</button>
      </div>
    </div>

    <!-- ====== 趋势图 ====== -->
    <div v-if="tab === 'trend'" class="card card-wide">
      <div class="row">
        <label>最近 <input type="number" v-model.number="trendLimit" min="3" max="100" style="width:60px" /> 次</label>
        <button @click="loadTrend()">加载</button>
      </div>

      <div class="trend-wrap" v-if="trendData.length">
        <svg viewBox="0 0 520 200" class="trend-svg" role="img" aria-label="质量趋势">
          <line x1="40" y1="20" x2="40" y2="176" class="axis" />
          <line x1="40" y1="176" x2="500" y2="176" class="axis" />
          <!-- 质量评分折线 -->
          <polyline v-if="trendScorePoints" :points="trendScorePoints" class="trend-line-score" />
          <!-- 问题总数折线 -->
          <polyline v-if="trendIssuesPoints" :points="trendIssuesPoints" class="trend-line-issues" />
          <!-- 数据点 -->
          <g v-for="(p, idx) in trendPointObjects" :key="idx">
            <circle :cx="p.x" :cy="p.scoreY" r="3" class="trend-dot-score" />
            <circle :cx="p.x" :cy="p.issuesY" r="2.5" class="trend-dot-issues" />
          </g>
        </svg>
        <div class="legend">
          <span class="legend-item"><span class="legend-color score-color"></span> 质量评分</span>
          <span class="legend-item"><span class="legend-color issues-color"></span> 问题总数</span>
        </div>
      </div>
      <p v-else class="muted">暂无趋势数据，请先运行分析。</p>

      <table class="tbl" v-if="trendData.length">
        <thead>
          <tr>
            <th>时间</th>
            <th>评分</th>
            <th>问题总数</th>
            <th>新增</th>
            <th>修复</th>
            <th>密度/KLOC</th>
            <th>退化</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="t in trendData" :key="t.run_id">
            <td>{{ t.started_at }}</td>
            <td><strong :class="scoreTextClass(t.score)">{{ formatScore(t.score) }}</strong></td>
            <td>{{ t.issues_total }}</td>
            <td class="text-green">+{{ t.issues_new }}</td>
            <td class="text-red">-{{ t.issues_fixed }}</td>
            <td>{{ formatDensity(t.density_per_kloc) }}</td>
            <td><span v-if="t.degraded" class="badge badge-error">⚠ 退化</span><span v-else>—</span></td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- ====== Top 排行 ====== -->
    <div v-if="tab === 'top'" class="card card-wide">
      <div class="row">
        <select v-model="topBy" @change="loadTopList()">
          <option value="file">按文件</option>
          <option value="dir">按目录</option>
          <option value="rule">按规则</option>
        </select>
        <label>Top <input type="number" v-model.number="topLimit" min="5" max="100" style="width:60px" /></label>
        <button @click="loadTopList()">刷新</button>
      </div>

      <table class="tbl" v-if="topItems.length">
        <thead>
          <tr>
            <th>{{ topBy === 'rule' ? '规则ID' : topBy === 'dir' ? '目录' : '文件' }}</th>
            <th>总计</th>
            <th>Error 数</th>
            <th>Error 占比</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="item in topItems" :key="item.name">
            <td :title="item.name">{{ topBy === 'file' ? shortenPath(item.name) : item.name }}</td>
            <td>{{ item.total }}</td>
            <td class="text-red">{{ item.errors }}</td>
            <td>
              <div class="mini-bar-wrap">
                <div class="mini-bar" :style="{ width: (item.total > 0 ? (item.errors / item.total * 100) : 0) + '%' }"></div>
                <span>{{ item.total > 0 ? (item.errors / item.total * 100).toFixed(0) : 0 }}%</span>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">暂无排行数据。</p>
    </div>

    <!-- ====== 分析任务 ====== -->
    <div v-if="tab === 'tasks'" class="card card-wide">
      <div class="row">
        <button :disabled="busy" @click="createTask" class="btn-primary">+ 新建分析任务</button>
        <button :disabled="busy" @click="loadTasks()">刷新列表</button>
        <select v-model="taskFilter.status" @change="loadTasks()">
          <option value="">全部状态</option>
          <option value="Pending">待执行</option>
          <option value="Running">运行中</option>
          <option value="Finished">已完成</option>
          <option value="Failed">失败</option>
        </select>
      </div>

      <table class="tbl" v-if="taskItems.length">
        <thead>
          <tr>
            <th>ID</th>
            <th>分支</th>
            <th>工具</th>
            <th>模式</th>
            <th>周期</th>
            <th>状态</th>
            <th>上次运行</th>
            <th>创建时间</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="t in taskItems" :key="t.id">
            <td>{{ t.id }}</td>
            <td>{{ t.branch }}</td>
            <td>{{ t.tools }}</td>
            <td>{{ t.mode }}</td>
            <td>{{ t.schedule }}</td>
            <td><span class="pill-sm" :class="'task-' + (t.status || '').toLowerCase()">{{ t.status }}</span></td>
            <td>{{ t.last_run_id ?? '—' }}</td>
            <td>{{ t.created_at }}</td>
            <td class="actions-cell">
              <button v-if="t.status !== 'Running'" @click="runTask(t.id)" class="btn-sm">▶ 运行</button>
              <button @click="deleteTask(t.id)" class="btn-sm btn-danger">删除</button>
            </td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">暂无分析任务。</p>
    </div>

    <!-- ====== 运行记录 ====== -->
    <div v-if="tab === 'runs'" class="card card-wide">
      <div class="row">
        <button :disabled="busy" @click="loadRuns()">刷新</button>
      </div>

      <table class="tbl" v-if="runItems.length">
        <thead>
          <tr>
            <th>ID</th>
            <th>任务</th>
            <th>工具</th>
            <th>状态</th>
            <th>文件</th>
            <th>行数</th>
            <th>问题</th>
            <th>新增</th>
            <th>修复</th>
            <th>评分</th>
            <th>退化</th>
            <th>时间</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="r in runItems" :key="r.id">
            <td>{{ r.id }}</td>
            <td>{{ r.task_id ?? '即时' }}</td>
            <td>{{ r.tools }}</td>
            <td><span class="pill-sm" :class="'task-' + (r.status || '').toLowerCase()">{{ r.status }}</span></td>
            <td>{{ r.analyzed_files }}</td>
            <td>{{ (r.lines_analyzed ?? 0).toLocaleString() }}</td>
            <td>{{ r.issues_total }}</td>
            <td class="text-green">+{{ r.issues_new }}</td>
            <td class="text-red">-{{ r.issues_fixed }}</td>
            <td><strong :class="scoreTextClass(r.score)">{{ formatScore(r.score) }}</strong></td>
            <td><span v-if="r.degraded" class="badge badge-error">⚠</span><span v-else>—</span></td>
            <td>{{ r.started_at }}</td>
            <td><button @click="deleteRun(r.id)" class="btn-sm btn-danger">删除</button></td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">暂无运行记录。</p>
    </div>

    <!-- ====== 质量基线 ====== -->
    <div v-if="tab === 'baseline'" class="card card-wide">
      <h3>质量基线配置</h3>
      <p class="muted">设置质量底线，超过阈值时将标记为"质量退化"。</p>

      <div class="form-grid" v-if="baseline">
        <div class="form-item">
          <label>最低质量评分</label>
          <input type="number" v-model.number="baselineForm.min_score" min="0" max="100" step="1" />
        </div>
        <div class="form-item">
          <label>最大新增问题数</label>
          <input type="number" v-model.number="baselineForm.max_new_issues" min="0" />
        </div>
        <div class="form-item">
          <label>最大 Error 问题数</label>
          <input type="number" v-model.number="baselineForm.max_error_issues" min="0" />
        </div>
        <div class="form-item form-actions">
          <button @click="saveBaseline()" class="btn-primary">保存基线</button>
        </div>
      </div>
      <div v-if="baseline?.configured" class="muted">
        当前基线: 评分 ≥ {{ baseline.min_score }}, 新增 ≤ {{ baseline.max_new_issues }}, Error ≤ {{ baseline.max_error_issues }}
        <br/>更新于: {{ baseline.updated_at }}
      </div>
    </div>
  </section>
</template>

<script>
import { apiGet, apiPost, apiPut, apiDelete, ApiError } from '../api/client'

export default {
  name: 'QualityView',
  props: {
    id: { type: String, required: true },
  },
  data() {
    return {
      busy: false,
      err: '',

      // 分析触发
      analysisTool: 'cppcheck',
      analysisMode: 'full',
      analysisMaxFiles: 2000,
      analyzeResult: null,

      // 质量总览
      summary: { quality: {}, baseline: {} },

      // 问题列表
      tab: 'issues',
      issues: [],
      issueTotal: 0,
      issuePage: 0,
      pageSize: 50,
      issueFilter: { tool: '', severity: '', status: 'active' },

      // 趋势
      trendData: [],
      trendLimit: 20,

      // Top
      topItems: [],
      topBy: 'file',
      topLimit: 20,

      // 任务
      taskItems: [],
      taskFilter: { status: '' },

      // 运行
      runItems: [],

      // 基线
      baseline: null,
      baselineForm: { min_score: 80, max_new_issues: 0, max_error_issues: 0 },
    }
  },
  computed: {
    repoId() { return Number(this.id) },

    severityEntries() {
      const sev = this.summary.quality?.severity ?? {}
      return Object.entries(sev).sort((a, b) => b[1] - a[1])
    },
    maxSeverityCount() {
      return Math.max(1, ...this.severityEntries.map(([, c]) => c))
    },
    baselineStatusText() {
      const b = this.summary.baseline ?? {}
      const score = this.summary.quality?.score ?? 0
      if (b.score_degraded) {
        return `质量退化 — 评分 ${this.formatScore(score)} 低于基线 ${b.min_score} 分`
      }
      if (b.error_degraded) {
        return `质量退化 — Error 问题 ${b.active_error_issues ?? 0} 个，超过基线 ${b.max_error_issues} 个`
      }
      return `质量退化 — 未满足质量基线`
    },

    trendPointObjects() {
      if (!this.trendData.length) return []
      const width = 520; const height = 200
      const left = 40; const right = 20; const top = 20; const bottom = 24
      const innerW = width - left - right
      const innerH = height - top - bottom
      const n = this.trendData.length

      const scores = this.trendData.map(t => Number(t.score ?? 0))
      const totals = this.trendData.map(t => Number(t.issues_total ?? 0))
      const maxScore = Math.max(100, ...scores)
      const maxTotal = Math.max(1, ...totals)

      return this.trendData.map((t, idx) => {
        const x = n <= 1 ? left + innerW / 2 : left + (idx * innerW) / (n - 1)
        const scoreY = top + (1 - Number(t.score ?? 0) / maxScore) * innerH
        const issuesY = top + (1 - Number(t.issues_total ?? 0) / maxTotal) * innerH
        return { x, scoreY, issuesY }
      })
    },
    trendScorePoints() {
      return this.trendPointObjects.map(p => `${p.x},${p.scoreY}`).join(' ')
    },
    trendIssuesPoints() {
      return this.trendPointObjects.map(p => `${p.x},${p.issuesY}`).join(' ')
    },
  },
  mounted() {
    this.loadSummary()
  },
  methods: {
    async loadSummary() {
      try {
        this.summary = await apiGet(`/api/repos/${this.repoId}/quality/summary`)
        // Also load baseline as fallback
        if (!this.summary.baseline?.configured) {
          const bl = await apiGet(`/api/repos/${this.repoId}/quality/baseline`)
          if (bl.configured) this.summary.baseline = bl
        }
      } catch (e) { /* ignore */ }
    },

    async triggerAnalysis() {
      this.busy = true
      this.err = ''
      this.analyzeResult = null
      try {
        const body = {
          tool: this.analysisTool,
          tools: this.analysisTool,
          ref: 'main',
          mode: this.analysisMode,
          max_files: this.analysisMaxFiles,
        }
        this.analyzeResult = await apiPost(`/api/repos/${this.repoId}/quality/analyze`, body)
        await this.loadSummary()
        if (this.tab === 'issues') await this.loadIssues()
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },

    async loadIssues() {
      try {
        const params = new URLSearchParams()
        if (this.issueFilter.tool) params.set('tool', this.issueFilter.tool)
        if (this.issueFilter.severity) params.set('severity', this.issueFilter.severity)
        if (this.issueFilter.status) params.set('status', this.issueFilter.status)
        params.set('limit', String(this.pageSize))
        params.set('offset', String(this.issuePage * this.pageSize))

        const data = await apiGet(`/api/repos/${this.repoId}/quality/issues?${params.toString()}`)
        this.issues = data.items ?? []
        // Estimate total from returned items (backend doesn't return total count)
        this.issueTotal = this.issues.length >= this.pageSize
          ? (this.issuePage + 2) * this.pageSize
          : this.issuePage * this.pageSize + this.issues.length
      } catch (e) {
        this.err = this.formatErr(e)
      }
    },

    async updateIssueStatus(issue, newStatus) {
      if (!newStatus) return
      try {
        await apiPut(`/api/quality/issues/${issue.id}`, { status: newStatus })
        issue.status = newStatus
        if (newStatus === 'fixed') issue.fixed_at = new Date().toISOString()
        await this.loadSummary()
      } catch (e) {
        this.err = this.formatErr(e)
      }
    },

    async loadTrend() {
      try {
        const data = await apiGet(`/api/repos/${this.repoId}/quality/trend?limit=${this.trendLimit}`)
        this.trendData = data.items ?? []
      } catch (e) { this.err = this.formatErr(e) }
    },

    async loadTopList() {
      try {
        const data = await apiGet(`/api/repos/${this.repoId}/quality/top?by=${this.topBy}&limit=${this.topLimit}`)
        this.topItems = data.items ?? []
      } catch (e) { this.err = this.formatErr(e) }
    },

    async loadTasks() {
      try {
        let url = `/api/repos/${this.repoId}/quality/tasks?limit=50`
        if (this.taskFilter.status) url += `&status=${this.taskFilter.status}`
        const data = await apiGet(url)
        this.taskItems = data.items ?? []
      } catch (e) { this.err = this.formatErr(e) }
    },

    async createTask() {
      try {
        await apiPost(`/api/repos/${this.repoId}/quality/tasks`, {
          branch: 'main',
          tools: this.analysisTool,
          mode: this.analysisMode,
          max_files: this.analysisMaxFiles,
          schedule: 'manual',
          run_now: true,
        })
        await this.loadTasks()
        await this.loadSummary()
      } catch (e) { this.err = this.formatErr(e) }
    },

    async runTask(taskId) {
      try {
        await apiPost(`/api/quality/tasks/${taskId}/run`)
        await this.loadTasks()
        await this.loadSummary()
      } catch (e) { this.err = this.formatErr(e) }
    },

    async deleteTask(taskId) {
      if (!confirm(`确定删除任务 #${taskId}？`)) return
      try {
        await apiDelete(`/api/quality/tasks/${taskId}`)
        await this.loadTasks()
      } catch (e) { this.err = this.formatErr(e) }
    },

    async loadRuns() {
      try {
        const data = await apiGet(`/api/repos/${this.repoId}/quality/runs?limit=30`)
        this.runItems = data.items ?? []
      } catch (e) { this.err = this.formatErr(e) }
    },

    async deleteRun(runId) {
      if (!confirm(`确定删除运行记录 #${runId}？`)) return
      try {
        await apiDelete(`/api/quality/runs/${runId}`)
        await this.loadRuns()
      } catch (e) { this.err = this.formatErr(e) }
    },

    async loadBaseline() {
      try {
        this.baseline = await apiGet(`/api/repos/${this.repoId}/quality/baseline`)
        this.baselineForm = {
          min_score: this.baseline.min_score ?? 80,
          max_new_issues: this.baseline.max_new_issues ?? 0,
          max_error_issues: this.baseline.max_error_issues ?? 0,
        }
      } catch (e) { this.err = this.formatErr(e) }
    },

    async saveBaseline() {
      try {
        await apiPut(`/api/repos/${this.repoId}/quality/baseline`, this.baselineForm)
        await this.loadBaseline()
        await this.loadSummary()
      } catch (e) { this.err = this.formatErr(e) }
    },

    // ---- helpers ----
    formatScore(s) { return Number(s ?? 0).toFixed(1) },
    formatDensity(d) { return Number(d ?? 0).toFixed(2) },
    shortenPath(p) {
      if (!p) return ''
      if (p.length <= 50) return p
      return '…' + p.slice(-49)
    },
    barWidth(count, max) { return max > 0 ? (count / max * 100).toFixed(1) + '%' : '0%' },
    statusLabel(s) {
      const map = { active: '活跃', fixed: '已修复', ignored: '已忽略', false_positive: '误报' }
      return map[s] ?? s
    },
    scoreLevelClass(score) {
      if (score >= 80) return 'score-good'
      if (score >= 60) return 'score-warn'
      return 'score-bad'
    },
    scoreTextClass(score) {
      if (score >= 80) return 'text-green'
      if (score >= 60) return 'text-orange'
      return 'text-red'
    },
    formatErr(e) {
      if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
      if (e instanceof Error) return e.message
      return String(e)
    },
  },
}
</script>

<style scoped>
.row { display: flex; gap: 12px; align-items: center; margin-bottom: 12px; }
.row-wrap { flex-wrap: wrap; }
.row-center { justify-content: center; }
.grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.card { border: 1px solid #e5e7eb; padding: 16px; border-radius: 8px; background: #fff; }
.card-wide { grid-column: 1 / -1; margin-top: 12px; }
.tbl { border-collapse: collapse; width: 100%; font-size: 14px; }
.tbl th, .tbl td { border: 1px solid #ddd; padding: 6px 8px; text-align: left; }
.tbl th { background: #f9fafb; font-weight: 600; }
.err { color: #b00020; white-space: pre-wrap; }
.muted { color: #6b7280; font-size: 14px; }

/* 评分显示 */
.score-display { display: flex; gap: 20px; align-items: center; }
.score-circle {
  width: 80px; height: 80px; border-radius: 50%;
  display: flex; flex-direction: column; align-items: center; justify-content: center;
  border: 4px solid #e5e7eb;
}
.score-circle.score-good { border-color: #10b981; background: #d1fae5; }
.score-circle.score-warn { border-color: #f59e0b; background: #fef3c7; }
.score-circle.score-bad { border-color: #ef4444; background: #fee2e2; }
.score-num { font-size: 22px; font-weight: 700; }
.score-label { font-size: 11px; color: #6b7280; }
.score-meta { font-size: 13px; line-height: 1.8; }

.baseline-info { margin-top: 8px; padding: 6px 10px; border-radius: 6px; font-size: 13px; background: #d1fae5; color: #065f46; }
.baseline-info.degraded { background: #fee2e2; color: #991b1b; }

/* 严重性条 */
.severity-bars { display: flex; flex-direction: column; gap: 6px; }
.sev-row { display: flex; align-items: center; gap: 8px; }
.sev-label { width: 72px; font-size: 12px; font-weight: 600; text-transform: uppercase; }
.sev-label.sev-error { color: #ef4444; }
.sev-label.sev-warning { color: #f59e0b; }
.sev-label.sev-style { color: #8b5cf6; }
.sev-label.sev-performance { color: #3b82f6; }
.sev-label.sev-portability { color: #06b6d4; }
.sev-label.sev-information { color: #6b7280; }
.sev-bar-wrap { flex: 1; height: 14px; background: #f3f4f6; border-radius: 7px; overflow: hidden; }
.sev-bar { height: 100%; border-radius: 7px; }
.sev-bar.sev-error { background: #ef4444; }
.sev-bar.sev-warning { background: #f59e0b; }
.sev-bar.sev-style { background: #8b5cf6; }
.sev-bar.sev-performance { background: #3b82f6; }
.sev-bar.sev-portability { background: #06b6d4; }
.sev-bar.sev-information { background: #9ca3af; }
.sev-count { width: 40px; text-align: right; font-weight: 600; font-size: 14px; }

/* 操作栏 */
.action-bar { padding: 10px 16px; }

/* 标签页 */
.tabs { display: flex; gap: 2px; margin-top: 16px; margin-bottom: 0; }
.tabs button {
  padding: 8px 18px; border: 1px solid #d1d5db; background: #f9fafb;
  cursor: pointer; font-size: 14px; border-radius: 6px 6px 0 0;
  border-bottom: none; color: #374151;
}
.tabs button.active { background: #fff; border-color: #e5e7eb; font-weight: 600; color: #111827; }
.tabs button:hover:not(.active) { background: #f3f4f6; }

/* 徽章 */
.badge {
  display: inline-block; padding: 2px 8px; border-radius: 999px;
  font-size: 11px; font-weight: 600; text-transform: uppercase;
}
.badge-error { background: #fee2e2; color: #991b1b; }
.badge-warning { background: #fef3c7; color: #92400e; }
.badge-style { background: #ede9fe; color: #5b21b6; }
.badge-performance { background: #dbeafe; color: #1e40af; }
.badge-portability { background: #cffafe; color: #155e75; }
.badge-information { background: #f3f4f6; color: #374151; }

.pill-sm {
  display: inline-block; padding: 1px 8px; border-radius: 999px;
  font-size: 11px; font-weight: 500;
}
.status-active { background: #d1fae5; color: #065f46; }
.status-fixed { background: #f3f4f6; color: #6b7280; }
.status-ignored { background: #fef3c7; color: #92400e; }
.status-false_positive { background: #fee2e2; color: #991b1b; }

/* 文本颜色 */
.text-green { color: #059669; }
.text-red { color: #dc2626; }
.text-orange { color: #d97706; }

/* 文件/消息单元格 */
.file-cell { max-width: 200px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-family: monospace; font-size: 12px; }
.msg-cell { max-width: 280px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.actions-cell { white-space: nowrap; }
.action-select { font-size: 12px; padding: 2px 4px; max-width: 100px; }

.row-fixed { opacity: 0.5; }

.btn-primary { background: #2563eb; color: #fff; border: none; padding: 8px 16px; border-radius: 6px; cursor: pointer; font-size: 14px; }
.btn-primary:hover { background: #1d4ed8; }
.btn-sm { padding: 4px 10px; border: 1px solid #d1d5db; border-radius: 4px; background: #fff; cursor: pointer; font-size: 12px; }
.btn-sm:hover { background: #f3f4f6; }
.btn-danger { color: #dc2626; border-color: #fca5a5; }
.btn-danger:hover { background: #fee2e2; }

.inline-label { font-size: 13px; }

/* 趋势图 */
.trend-wrap {
  border: 1px solid #e5e7eb; border-radius: 8px; padding: 8px;
  background: #fcfcfd; margin-bottom: 10px; overflow-x: auto;
}
.trend-svg { width: 100%; min-width: 500px; height: 200px; display: block; }
.axis { stroke: #cbd5e1; stroke-width: 1; }
.trend-line-score {
  fill: none; stroke: #2563eb; stroke-width: 2.5;
  stroke-linecap: round; stroke-linejoin: round;
}
.trend-line-issues {
  fill: none; stroke: #f59e0b; stroke-width: 2;
  stroke-linecap: round; stroke-linejoin: round; stroke-dasharray: 6 3;
}
.trend-dot-score { fill: #2563eb; }
.trend-dot-issues { fill: #f59e0b; }

.legend { display: flex; gap: 16px; margin-top: 6px; font-size: 12px; }
.legend-item { display: flex; align-items: center; gap: 4px; }
.legend-color { display: inline-block; width: 14px; height: 3px; border-radius: 2px; }
.score-color { background: #2563eb; }
.issues-color { background: #f59e0b; }

/* Mini bar */
.mini-bar-wrap { display: flex; align-items: center; gap: 4px; }
.mini-bar { height: 8px; background: #ef4444; border-radius: 4px; min-width: 2px; }

/* 表单 */
.form-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 12px; margin: 12px 0; }
.form-item { display: flex; flex-direction: column; gap: 4px; }
.form-item label { font-size: 13px; font-weight: 500; color: #374151; }
.form-item input { padding: 6px 10px; border: 1px solid #d1d5db; border-radius: 6px; font-size: 14px; }
.form-actions { justify-content: flex-end; flex-direction: row; align-items: flex-end; }

/* 任务状态 */
.task-pending { background: #f3f4f6; color: #374151; }
.task-running { background: #dbeafe; color: #1e40af; }
.task-finished { background: #d1fae5; color: #065f46; }
.task-failed { background: #fee2e2; color: #991b1b; }
</style>

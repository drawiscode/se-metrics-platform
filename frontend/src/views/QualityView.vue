<template>
  <section class="quality-page">
    <div class="page-header">
      <div>
        <p class="eyebrow">Quality Analysis</p>
        <h2>代码质量分析</h2>
        <div class="page-meta">
          <span>Repo #{{ repoId }}</span>
          <span v-if="summary.latest_run?.id">最近运行 #{{ summary.latest_run.id }}</span>
          <span v-if="summary.latest_run?.started_at">{{ summary.latest_run.started_at }}</span>
        </div>
      </div>
      <RouterLink class="back-link" :to="`/repos/${repoId}`">返回仓库</RouterLink>
    </div>

    <p v-if="err" class="err alert-block">{{ err }}</p>

    <!-- ====== 质量总览卡片 ====== -->
    <div class="grid" v-if="!err">
      <div class="card overview-card score-card">
        <div class="card-head">
          <h3>质量评分</h3>
          <span class="pill-sm" :class="scoreLevelClass(summary.quality?.score ?? 0)">
            {{ scoreLabel(summary.quality?.score ?? 0) }}
          </span>
        </div>
        <div class="score-display">
          <div class="score-circle" :class="scoreLevelClass(summary.quality?.score ?? 0)" :style="scoreRingStyle">
            <span class="score-num">{{ formatScore(summary.quality?.score) }}</span>
            <span class="score-label">/ 100</span>
          </div>
          <div class="metric-grid">
            <div class="metric-item">
              <span>问题总数</span>
              <strong>{{ summary.quality?.total_issues ?? 0 }}</strong>
            </div>
            <div class="metric-item">
              <span>涉及文件</span>
              <strong>{{ summary.quality?.files_with_issues ?? 0 }}</strong>
            </div>
            <div class="metric-item">
              <span>密度/KLOC</span>
              <strong>{{ formatDensity(summary.density_per_kloc) }}</strong>
            </div>
            <div class="metric-item">
              <span>分析行数</span>
              <strong>{{ formatNumber(summary.lines_analyzed) }}</strong>
            </div>
          </div>
        </div>
        <div v-if="summary.baseline?.configured" class="baseline-info" :class="{ degraded: summary.baseline.degraded }">
          <span v-if="summary.baseline.degraded">{{ baselineStatusText }}</span>
          <span v-else>质量达标，基线 {{ summary.baseline.min_score }} 分</span>
        </div>
      </div>

      <div class="card overview-card">
        <div class="card-head">
          <h3>严重性分布</h3>
          <span class="muted">{{ severityEntries.length }} 类</span>
        </div>
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

      <div class="card overview-card">
        <div class="card-head">
          <h3>质量洞察</h3>
          <span v-if="insights" class="pill-sm" :class="'risk-' + insights.risk_level">{{ riskLabel(insights.risk_level) }}</span>
        </div>
        <div v-if="insights" class="insight-panel">
          <div class="risk-line">
            <span>评分变化 {{ formatDelta(insights.trend?.score_delta) }}</span>
            <span>活跃问题 {{ insights.active_issues ?? 0 }}</span>
            <span>Error {{ insights.active_errors ?? 0 }}</span>
          </div>
          <div v-if="insights.top_file" class="insight-kv">
            <span>热点文件</span>
            <strong :title="insights.top_file.path">{{ shortenPath(insights.top_file.path) }}</strong>
          </div>
          <div v-if="insights.top_rule" class="insight-kv">
            <span>高频规则</span>
            <strong>{{ insights.top_rule.rule_id }} × {{ insights.top_rule.total }}</strong>
          </div>
          <ul class="insight-actions">
            <li v-for="(action, idx) in insights.actions" :key="idx">{{ action }}</li>
          </ul>
        </div>
        <p v-else class="muted">暂无洞察数据</p>
      </div>
    </div>

    <!-- ====== 操作栏 ====== -->
    <div class="card card-wide action-bar">
      <div class="toolbar">
        <div class="toolbar-fields">
          <label class="field">
            <span>工具</span>
            <select v-model="analysisTool">
              <option value="cppcheck">cppcheck</option>
              <option value="clang-tidy">clang-tidy</option>
              <option value="cpplint">cpplint</option>
              <option value="flawfinder">flawfinder</option>
              <option value="pylint">pylint</option>
              <option value="checkstyle">checkstyle</option>
              <option value="all">全部工具</option>
            </select>
          </label>
          <label class="field">
            <span>模式</span>
            <select v-model="analysisMode">
              <option value="full">全量模式</option>
              <option value="hot">热点模式</option>
            </select>
          </label>
          <label class="field field-compact">
            <span>文件上限</span>
            <input type="number" v-model.number="analysisMaxFiles" min="10" max="10000" />
          </label>
        </div>
        <button :disabled="busy" @click="triggerAnalysis" class="btn-primary">
          {{ busy ? '分析中' : '运行分析' }}
        </button>
      </div>

      <div v-if="analyzeResult" class="run-result" :class="{ failed: analyzeResult.status !== 'Finished' }">
        <strong>{{ analyzeResult.status === 'Finished' ? '分析完成' : '分析失败' }}</strong>
        <span>文件 {{ analyzeResult.analyzed_files }}</span>
        <span>行数 {{ formatNumber(analyzeResult.lines_analyzed) }}</span>
        <span>新增 {{ analyzeResult.issues_new ?? 0 }}</span>
        <span>修复 {{ analyzeResult.issues_fixed ?? 0 }}</span>
      </div>
      <span v-if="analyzeResult?.error" class="err">{{ analyzeResult.error }}</span>
    </div>

    <div class="quick-stats" v-if="!err">
      <div>
        <span>当前筛选</span>
        <strong>{{ issueFilter.tool || '全部工具' }}</strong>
      </div>
      <div>
        <span>活跃 Error</span>
        <strong>{{ summary.baseline?.active_error_issues ?? 0 }}</strong>
      </div>
      <div>
        <span>最新新增</span>
        <strong>{{ summary.latest_run?.issues_new ?? 0 }}</strong>
      </div>
    </div>

    <!-- ====== 标签页切换 ====== -->
    <div class="tabs" role="tablist" aria-label="质量分析视图">
      <button :class="{ active: tab === 'issues' }" @click="tab = 'issues'; loadIssues()">问题列表</button>
      <button :class="{ active: tab === 'trend' }" @click="tab = 'trend'; loadTrend()">趋势图</button>
      <button :class="{ active: tab === 'top' }" @click="tab = 'top'; loadTopList()">Top 排行</button>
      <button :class="{ active: tab === 'tasks' }" @click="tab = 'tasks'; loadTasks()">分析任务</button>
      <button :class="{ active: tab === 'runs' }" @click="tab = 'runs'; loadRuns()">运行记录</button>
      <button :class="{ active: tab === 'baseline' }" @click="tab = 'baseline'; loadBaseline()">质量基线</button>
    </div>

    <!-- ====== 问题列表 ====== -->
    <div v-if="tab === 'issues'" class="card card-wide panel-card">
      <div class="panel-head">
        <div>
          <h3>问题列表</h3>
          <p class="muted">共 {{ issueTotal }} 条，当前 {{ issues.length }} 条</p>
        </div>
        <div class="filter-row">
          <select v-model="issueFilter.tool" @change="reloadIssuesFromFirstPage()">
            <option value="">全部工具</option>
            <option value="cppcheck">cppcheck</option>
            <option value="clang-tidy">clang-tidy</option>
            <option value="cpplint">cpplint</option>
            <option value="flawfinder">flawfinder</option>
            <option value="pylint">pylint</option>
            <option value="checkstyle">checkstyle</option>
          </select>
          <select v-model="issueFilter.severity" @change="reloadIssuesFromFirstPage()">
            <option value="">全部等级</option>
            <option value="error">error</option>
            <option value="warning">warning</option>
            <option value="style">style</option>
            <option value="performance">performance</option>
            <option value="portability">portability</option>
            <option value="information">information</option>
          </select>
          <select v-model="issueFilter.status" @change="reloadIssuesFromFirstPage()">
            <option value="active">活跃问题</option>
            <option value="fixed">已修复</option>
            <option value="ignored">已忽略</option>
            <option value="false_positive">误报</option>
            <option value="all">全部状态</option>
          </select>
        </div>
      </div>

      <div class="table-wrap" v-if="issues.length">
        <table class="tbl">
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
              <td class="mono-cell">{{ iss.line }}:{{ iss.column }}</td>
              <td><span class="pill-sm tool-pill">{{ iss.tool }}</span></td>
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
      </div>
      <div v-else class="empty-state">暂无问题数据</div>

      <div class="row row-center" v-if="issueTotal > pageSize">
        <button :disabled="issuePage <= 0" @click="issuePage--; loadIssues()">上一页</button>
        <span>第 {{ issuePage + 1 }} 页 / 共 {{ Math.ceil(issueTotal / pageSize) }} 页</span>
        <button :disabled="(issuePage + 1) * pageSize >= issueTotal" @click="issuePage++; loadIssues()">下一页</button>
      </div>
    </div>

    <!-- ====== 趋势图 ====== -->
    <div v-if="tab === 'trend'" class="card card-wide panel-card">
      <div class="panel-head">
        <div>
          <h3>趋势图</h3>
          <p class="muted">最近完成的质量分析运行</p>
        </div>
        <label class="field field-inline">最近 <input type="number" v-model.number="trendLimit" min="3" max="100" /> 次</label>
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
      <div v-else class="empty-state">暂无趋势数据，请先运行分析</div>

      <div class="table-wrap" v-if="trendData.length">
        <table class="tbl">
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
              <td><span v-if="t.degraded" class="badge badge-error">退化</span><span v-else>—</span></td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- ====== Top 排行 ====== -->
    <div v-if="tab === 'top'" class="card card-wide panel-card">
      <div class="panel-head">
        <div>
          <h3>Top 排行</h3>
          <p class="muted">按文件、目录或规则定位主要问题来源</p>
        </div>
        <div class="filter-row">
        <select v-model="topBy" @change="loadTopList()">
          <option value="file">按文件</option>
          <option value="dir">按目录</option>
          <option value="rule">按规则</option>
        </select>
        <label class="field field-inline">Top <input type="number" v-model.number="topLimit" min="5" max="100" /></label>
        <button @click="loadTopList()">刷新</button>
        </div>
      </div>

      <div class="table-wrap" v-if="topItems.length">
        <table class="tbl">
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
      </div>
      <div v-else class="empty-state">暂无排行数据</div>
    </div>

    <!-- ====== 分析任务 ====== -->
    <div v-if="tab === 'tasks'" class="card card-wide panel-card">
      <div class="panel-head">
        <div>
          <h3>分析任务</h3>
          <p class="muted">管理可重复运行的质量分析任务</p>
        </div>
        <div class="filter-row">
        <button :disabled="busy" @click="createTask" class="btn-primary">新建任务</button>
        <button :disabled="busy" @click="loadTasks()">刷新列表</button>
        <select v-model="taskFilter.status" @change="loadTasks()">
          <option value="">全部状态</option>
          <option value="Pending">待执行</option>
          <option value="Running">运行中</option>
          <option value="Finished">已完成</option>
          <option value="Failed">失败</option>
        </select>
        </div>
      </div>

      <div class="table-wrap" v-if="taskItems.length">
        <table class="tbl">
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
                <button v-if="t.status !== 'Running'" @click="runTask(t.id)" class="btn-sm">运行</button>
                <button @click="deleteTask(t.id)" class="btn-sm btn-danger">删除</button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <div v-else class="empty-state">暂无分析任务</div>
    </div>

    <!-- ====== 运行记录 ====== -->
    <div v-if="tab === 'runs'" class="card card-wide panel-card">
      <div class="panel-head">
        <div>
          <h3>运行记录</h3>
          <p class="muted">查看每次分析的范围、结果和退化情况</p>
        </div>
        <button :disabled="busy" @click="loadRuns()">刷新</button>
      </div>

      <div class="table-wrap" v-if="runItems.length">
        <table class="tbl">
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
              <td>{{ formatNumber(r.lines_analyzed) }}</td>
              <td>{{ r.issues_total }}</td>
              <td class="text-green">+{{ r.issues_new }}</td>
              <td class="text-red">-{{ r.issues_fixed }}</td>
              <td><strong :class="scoreTextClass(r.score)">{{ formatScore(r.score) }}</strong></td>
              <td><span v-if="r.degraded" class="badge badge-error">退化</span><span v-else>—</span></td>
              <td>{{ r.started_at }}</td>
              <td><button @click="deleteRun(r.id)" class="btn-sm btn-danger">删除</button></td>
            </tr>
          </tbody>
        </table>
      </div>
      <div v-else class="empty-state">暂无运行记录</div>
    </div>

    <!-- ====== 质量基线 ====== -->
    <div v-if="tab === 'baseline'" class="card card-wide panel-card">
      <div class="panel-head">
        <div>
          <h3>质量基线配置</h3>
          <p class="muted">设置质量底线，超过阈值时将标记为质量退化</p>
        </div>
      </div>

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
      insights: null,

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

    scoreRingStyle() {
      const score = Math.max(0, Math.min(100, Number(this.summary.quality?.score ?? 0)))
      return { '--score-deg': `${score * 3.6}deg` }
    },
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
      if (b.new_issues_degraded) {
        return `质量退化 — 最新运行新增 ${b.latest_new_issues ?? 0} 个问题，超过基线 ${b.max_new_issues} 个`
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
    this.loadInsights()
    this.loadIssues()
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

    async loadInsights() {
      try {
        this.insights = await apiGet(`/api/repos/${this.repoId}/quality/insights`)
      } catch (e) {
        this.insights = null
      }
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
        await this.loadInsights()
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
        this.issueTotal = data.total ?? this.issues.length
      } catch (e) {
        this.err = this.formatErr(e)
      }
    },

    async reloadIssuesFromFirstPage() {
      this.issuePage = 0
      await this.loadIssues()
    },

    async updateIssueStatus(issue, newStatus) {
      if (!newStatus) return
      try {
        await apiPut(`/api/quality/issues/${issue.id}`, { status: newStatus })
        issue.status = newStatus
        if (newStatus === 'fixed') issue.fixed_at = new Date().toISOString()
        await this.loadSummary()
        await this.loadInsights()
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
        await this.loadInsights()
      } catch (e) { this.err = this.formatErr(e) }
    },

    async runTask(taskId) {
      try {
        await apiPost(`/api/quality/tasks/${taskId}/run`)
        await this.loadTasks()
        await this.loadSummary()
        await this.loadInsights()
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
    formatNumber(n) { return Number(n ?? 0).toLocaleString() },
    formatDelta(v) {
      const n = Number(v ?? 0)
      const prefix = n > 0 ? '+' : ''
      return `${prefix}${n.toFixed(1)}`
    },
    scoreLabel(score) {
      if (score >= 80) return '良好'
      if (score >= 60) return '需关注'
      return '高风险'
    },
    riskLabel(level) {
      const map = { clean: '稳定', watch: '关注', critical: '高风险' }
      return map[level] ?? level
    },
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
.quality-page {
  --q-text: #172033;
  --q-muted: #667085;
  --q-soft: #f6f8fb;
  --q-panel: #ffffff;
  --q-border: #dfe5ef;
  --q-border-strong: #cbd5e1;
  --q-blue: #2563eb;
  --q-green: #059669;
  --q-amber: #b45309;
  --q-red: #dc2626;
  --q-cyan: #0891b2;
  --q-purple: #7c3aed;

  text-align: left;
  color: var(--q-text);
  padding: 24px;
  background: #f8fafc;
  min-height: 100%;
  box-sizing: border-box;
}

.quality-page h2,
.quality-page h3,
.quality-page p {
  margin: 0;
}

.page-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 20px;
  margin-bottom: 18px;
}

.eyebrow {
  color: var(--q-blue);
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0;
  text-transform: uppercase;
  margin-bottom: 6px;
}

.page-header h2 {
  color: var(--q-text);
  font-size: 28px;
  line-height: 1.2;
  letter-spacing: 0;
  margin-bottom: 8px;
}

.page-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  color: var(--q-muted);
  font-size: 13px;
}

.page-meta span,
.quick-stats div {
  border: 1px solid var(--q-border);
  background: var(--q-panel);
  border-radius: 8px;
  padding: 5px 9px;
}

.back-link {
  color: var(--q-blue);
  background: var(--q-panel);
  border: 1px solid var(--q-border);
  border-radius: 8px;
  padding: 8px 12px;
  text-decoration: none;
  font-size: 14px;
  font-weight: 600;
}

.back-link:hover {
  border-color: var(--q-blue);
}

.grid {
  display: grid;
  grid-template-columns: minmax(300px, 1.15fr) minmax(260px, 0.9fr) minmax(280px, 1fr);
  gap: 14px;
}

.card {
  background: var(--q-panel);
  border: 1px solid var(--q-border);
  border-radius: 8px;
  box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04);
}

.overview-card,
.panel-card {
  padding: 18px;
}

.card-wide {
  grid-column: 1 / -1;
  margin-top: 14px;
}

.card-head,
.panel-head,
.toolbar {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 14px;
}

.card-head,
.panel-head {
  margin-bottom: 14px;
}

.card-head h3,
.panel-head h3 {
  color: var(--q-text);
  font-size: 16px;
  line-height: 1.25;
  letter-spacing: 0;
}

.muted {
  color: var(--q-muted);
  font-size: 13px;
}

.alert-block {
  margin-bottom: 14px;
  padding: 12px 14px;
  border: 1px solid #fecaca;
  border-radius: 8px;
  background: #fef2f2;
}

.err {
  color: #b91c1c;
  white-space: pre-wrap;
}

.score-display {
  display: grid;
  grid-template-columns: 116px minmax(0, 1fr);
  align-items: center;
  gap: 18px;
}

.score-circle {
  --score-color: var(--q-blue);
  width: 108px;
  height: 108px;
  border-radius: 50%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  background:
    radial-gradient(circle at center, #fff 0 58%, transparent 59%),
    conic-gradient(var(--score-color) 0 var(--score-deg), #e5e7eb var(--score-deg) 360deg);
}

.score-circle.score-good { --score-color: var(--q-green); }
.score-circle.score-warn { --score-color: #d97706; }
.score-circle.score-bad { --score-color: var(--q-red); }

.score-num {
  color: var(--q-text);
  font-size: 26px;
  line-height: 1;
  font-weight: 800;
}

.score-label {
  color: var(--q-muted);
  font-size: 12px;
  margin-top: 4px;
}

.metric-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
}

.metric-item {
  min-width: 0;
  background: var(--q-soft);
  border: 1px solid #edf1f7;
  border-radius: 8px;
  padding: 9px 10px;
}

.metric-item span,
.quick-stats span,
.field span {
  display: block;
  color: var(--q-muted);
  font-size: 12px;
  line-height: 1.2;
  margin-bottom: 4px;
}

.metric-item strong,
.quick-stats strong {
  display: block;
  color: var(--q-text);
  font-size: 18px;
  line-height: 1.2;
}

.baseline-info {
  margin-top: 14px;
  padding: 9px 11px;
  border-radius: 8px;
  font-size: 13px;
  background: #ecfdf3;
  color: #047857;
  border: 1px solid #bbf7d0;
}

.baseline-info.degraded {
  background: #fef2f2;
  color: #b91c1c;
  border-color: #fecaca;
}

.severity-bars {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.sev-row {
  display: grid;
  grid-template-columns: 88px minmax(0, 1fr) 42px;
  align-items: center;
  gap: 10px;
}

.sev-label {
  font-size: 12px;
  font-weight: 700;
  text-transform: uppercase;
}

.sev-label.sev-error { color: var(--q-red); }
.sev-label.sev-warning { color: #d97706; }
.sev-label.sev-style { color: var(--q-purple); }
.sev-label.sev-performance { color: var(--q-blue); }
.sev-label.sev-portability { color: var(--q-cyan); }
.sev-label.sev-information { color: var(--q-muted); }

.sev-bar-wrap {
  height: 10px;
  background: #edf2f7;
  border-radius: 999px;
  overflow: hidden;
}

.sev-bar {
  height: 100%;
  border-radius: 999px;
}

.sev-bar.sev-error { background: var(--q-red); }
.sev-bar.sev-warning { background: #f59e0b; }
.sev-bar.sev-style { background: var(--q-purple); }
.sev-bar.sev-performance { background: var(--q-blue); }
.sev-bar.sev-portability { background: var(--q-cyan); }
.sev-bar.sev-information { background: #94a3b8; }

.sev-count {
  color: var(--q-text);
  text-align: right;
  font-size: 13px;
  font-weight: 700;
}

.insight-panel {
  display: flex;
  flex-direction: column;
  gap: 10px;
  font-size: 13px;
}

.risk-line,
.filter-row,
.toolbar-fields,
.run-result,
.quick-stats {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
}

.risk-line span {
  color: var(--q-muted);
  background: var(--q-soft);
  border: 1px solid #edf1f7;
  border-radius: 8px;
  padding: 5px 8px;
}

.insight-kv {
  display: grid;
  grid-template-columns: 68px minmax(0, 1fr);
  gap: 8px;
  align-items: center;
}

.insight-kv span {
  color: var(--q-muted);
}

.insight-kv strong {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.insight-actions {
  margin: 2px 0 0;
  padding-left: 18px;
  display: flex;
  flex-direction: column;
  gap: 5px;
  color: #334155;
}

.action-bar {
  padding: 16px 18px;
}

.toolbar {
  align-items: flex-end;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.field-inline {
  flex-direction: row;
  align-items: center;
  gap: 8px;
}

.field-inline span,
.field-inline {
  color: var(--q-muted);
  font-size: 13px;
}

.field-compact input {
  width: 96px;
}

select,
input {
  height: 34px;
  border: 1px solid var(--q-border-strong);
  border-radius: 7px;
  background: #fff;
  color: var(--q-text);
  padding: 0 10px;
  font-size: 13px;
  box-sizing: border-box;
}

select:focus,
input:focus {
  outline: 2px solid rgba(37, 99, 235, 0.18);
  border-color: var(--q-blue);
}

button,
.btn-primary,
.btn-sm {
  height: 34px;
  border-radius: 7px;
  border: 1px solid var(--q-border-strong);
  background: #fff;
  color: #344054;
  cursor: pointer;
  font-size: 13px;
  font-weight: 600;
  padding: 0 12px;
}

button:hover,
.btn-sm:hover {
  background: #f8fafc;
  border-color: #94a3b8;
}

button:disabled,
.btn-primary:disabled {
  cursor: not-allowed;
  opacity: 0.6;
}

.btn-primary {
  color: #fff;
  background: var(--q-blue);
  border-color: var(--q-blue);
}

.btn-primary:hover {
  background: #1d4ed8;
  border-color: #1d4ed8;
}

.btn-danger {
  color: var(--q-red);
  border-color: #fecaca;
}

.btn-danger:hover {
  background: #fef2f2;
  border-color: #fca5a5;
}

.run-result {
  margin-top: 12px;
  padding: 9px 11px;
  border-radius: 8px;
  color: #065f46;
  background: #ecfdf3;
  border: 1px solid #bbf7d0;
  font-size: 13px;
}

.run-result.failed {
  color: #b91c1c;
  background: #fef2f2;
  border-color: #fecaca;
}

.quick-stats {
  margin-top: 14px;
}

.quick-stats div {
  min-width: 132px;
}

.tabs {
  display: flex;
  gap: 6px;
  margin-top: 18px;
  overflow-x: auto;
  padding-bottom: 2px;
}

.tabs button {
  flex: 0 0 auto;
  border-color: transparent;
  background: transparent;
  color: var(--q-muted);
}

.tabs button.active {
  color: var(--q-blue);
  background: #eff6ff;
  border-color: #bfdbfe;
}

.panel-head {
  align-items: center;
}

.table-wrap {
  width: 100%;
  overflow-x: auto;
  border: 1px solid var(--q-border);
  border-radius: 8px;
}

.tbl {
  width: 100%;
  min-width: 840px;
  border-collapse: separate;
  border-spacing: 0;
  font-size: 13px;
}

.tbl th,
.tbl td {
  padding: 10px 12px;
  text-align: left;
  border-bottom: 1px solid #edf1f7;
  vertical-align: middle;
}

.tbl th {
  position: sticky;
  top: 0;
  background: #f8fafc;
  color: #475467;
  font-size: 12px;
  font-weight: 700;
  z-index: 1;
}

.tbl tbody tr:hover {
  background: #f9fbff;
}

.tbl tbody tr:last-child td {
  border-bottom: none;
}

code,
.mono-cell,
.file-cell {
  font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
}

code {
  display: inline-block;
  max-width: 180px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  border-radius: 6px;
  background: #f1f5f9;
  color: #334155;
  padding: 3px 6px;
  font-size: 12px;
}

.file-cell {
  max-width: 260px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #0f172a;
  font-size: 12px;
}

.msg-cell {
  max-width: 360px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #475467;
}

.actions-cell {
  white-space: nowrap;
}

.action-select {
  max-width: 124px;
}

.row {
  display: flex;
  gap: 12px;
  align-items: center;
  margin-bottom: 12px;
}

.row-center {
  justify-content: center;
  margin-top: 14px;
}

.row-fixed {
  opacity: 0.58;
}

.empty-state {
  border: 1px dashed var(--q-border-strong);
  border-radius: 8px;
  padding: 34px 16px;
  color: var(--q-muted);
  background: #fbfdff;
  text-align: center;
  font-size: 14px;
}

.badge,
.pill-sm {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-height: 22px;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 11px;
  font-weight: 700;
  line-height: 1;
  white-space: nowrap;
}

.badge-error,
.pill-sm.score-bad,
.risk-critical,
.status-false_positive,
.task-failed {
  background: #fef2f2;
  color: #b91c1c;
}

.badge-warning,
.pill-sm.score-warn,
.risk-watch,
.status-ignored {
  background: #fffbeb;
  color: #b45309;
}

.badge-style {
  background: #f5f3ff;
  color: #6d28d9;
}

.badge-performance,
.tool-pill,
.task-running {
  background: #eff6ff;
  color: #1d4ed8;
}

.badge-portability {
  background: #ecfeff;
  color: #0e7490;
}

.badge-information,
.status-fixed,
.task-pending {
  background: #f1f5f9;
  color: #475569;
}

.pill-sm.score-good,
.risk-clean,
.status-active,
.task-finished {
  background: #ecfdf3;
  color: #047857;
}

.text-green { color: var(--q-green); }
.text-red { color: var(--q-red); }
.text-orange { color: #d97706; }

.trend-wrap {
  border: 1px solid var(--q-border);
  border-radius: 8px;
  padding: 10px;
  background: #fbfdff;
  margin-bottom: 12px;
  overflow-x: auto;
}

.trend-svg {
  width: 100%;
  min-width: 500px;
  height: 210px;
  display: block;
}

.axis {
  stroke: #cbd5e1;
  stroke-width: 1;
}

.trend-line-score {
  fill: none;
  stroke: var(--q-blue);
  stroke-width: 2.5;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.trend-line-issues {
  fill: none;
  stroke: #f59e0b;
  stroke-width: 2;
  stroke-linecap: round;
  stroke-linejoin: round;
  stroke-dasharray: 6 3;
}

.trend-dot-score { fill: var(--q-blue); }
.trend-dot-issues { fill: #f59e0b; }

.legend {
  display: flex;
  gap: 16px;
  margin-top: 6px;
  font-size: 12px;
  color: var(--q-muted);
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 5px;
}

.legend-color {
  display: inline-block;
  width: 16px;
  height: 3px;
  border-radius: 2px;
}

.score-color { background: var(--q-blue); }
.issues-color { background: #f59e0b; }

.mini-bar-wrap {
  display: grid;
  grid-template-columns: minmax(80px, 1fr) 42px;
  align-items: center;
  gap: 8px;
  max-width: 220px;
}

.mini-bar {
  height: 8px;
  background: var(--q-red);
  border-radius: 999px;
  min-width: 2px;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 14px;
}

.form-item {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.form-item label {
  color: #344054;
  font-size: 13px;
  font-weight: 700;
}

.form-actions {
  justify-content: flex-end;
}

@media (prefers-color-scheme: dark) {
  .quality-page {
    --q-text: #e5e7eb;
    --q-muted: #9ca3af;
    --q-soft: #202631;
    --q-panel: #171c25;
    --q-border: #2e3745;
    --q-border-strong: #465466;
    background: #10141b;
  }

  select,
  input,
  button,
  .btn-sm,
  .back-link {
    background: #111827;
    color: var(--q-text);
  }

  .score-circle {
    background:
      radial-gradient(circle at center, #171c25 0 58%, transparent 59%),
      conic-gradient(var(--score-color) 0 var(--score-deg), #334155 var(--score-deg) 360deg);
  }

  .tbl th,
  .metric-item,
  .risk-line span,
  .empty-state,
  .trend-wrap {
    background: #111827;
  }

  code {
    background: #0f172a;
    color: #cbd5e1;
  }
}

@media (max-width: 1100px) {
  .grid {
    grid-template-columns: 1fr 1fr;
  }

  .score-card {
    grid-column: 1 / -1;
  }
}

@media (max-width: 760px) {
  .quality-page {
    padding: 16px;
  }

  .page-header,
  .toolbar,
  .panel-head {
    flex-direction: column;
    align-items: stretch;
  }

  .grid {
    grid-template-columns: 1fr;
  }

  .score-display {
    grid-template-columns: 1fr;
  }

  .metric-grid {
    grid-template-columns: 1fr 1fr;
  }

  .toolbar-fields,
  .filter-row {
    align-items: stretch;
  }

  .field,
  .filter-row select,
  .filter-row button,
  .toolbar-fields select,
  .toolbar-fields input {
    width: 100%;
  }
}
</style>

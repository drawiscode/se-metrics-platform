<template>
  <section>
    <div class="row row-between">
      <h2>系统日志</h2>
      <button :disabled="busy" @click="refreshAll">刷新</button>
    </div>

    <p v-if="err" class="err">{{ err }}</p>

    <div class="stats-grid">
      <div class="stat-card">
        <div class="label">今日请求数</div>
        <div class="num">{{ stats.today_request_count }}</div>
      </div>
      <div class="stat-card danger">
        <div class="label">今日错误数</div>
        <div class="num">{{ stats.today_error_count }}</div>
      </div>
      <div class="stat-card ai">
        <div class="label">今日 AI 调用次数</div>
        <div class="num">{{ stats.today_ai_call_count }}</div>
      </div>
    </div>

    <div class="tabbar">
      <button
        class="tab-btn"
        :class="{ active: tab === 'ops' }"
        @click="tab = 'ops'"
      >
        操作日志
      </button>
      <button
        class="tab-btn"
        :class="{ active: tab === 'ai' }"
        @click="tab = 'ai'"
      >
        AI 用量
      </button>
    </div>

    <div v-if="tab === 'ops'" class="card">
      <div class="row row-between">
        <b>操作日志</b>
        <div class="row compact-row">
          <label>开始</label>
          <input type="datetime-local" v-model="startTime" />
          <label>结束</label>
          <input type="datetime-local" v-model="endTime" />
          <label>状态</label>
          <select v-model="opsStatus" @change="loadOps">
            <option value="">全部</option>
            <option value="ok">ok</option>
            <option value="error">error</option>
          </select>
          <label>每页</label>
          <select v-model.number="perPage">
            <option :value="10">10</option>
            <option :value="20">20</option>
            <option :value="50">50</option>
          </select>
          <button :disabled="busy" @click="loadOps">查询</button>
        </div>
      </div>

      <table class="tbl" v-if="ops.length">
        <thead>
          <tr>
            <th>时间</th>
            <th>详情</th>
            <th>操作类型</th>
            <th>目标</th>
            <th>状态</th>
            <th>耗时(ms)</th>
            <th>IP</th>
          </tr>
        </thead>
        <tbody>
          <template v-for="row in opsPageItems()" :key="row.id">
            <tr>
              <td>{{ formatBeijingTime(row.time) }}</td>
              <td>
                <button class="link-btn" @click="toggleOpDetail(row.id)">
                  {{ expandedOps[row.id] ? '收起' : '详情' }}
                </button>
              </td>
              <td>{{ row.operation_type }}</td>
              <td>{{ row.target }}</td>
              <td>
                <span class="pill" :class="row.status === 'ok' ? 'ok' : 'error'">{{ row.status }}</span>
              </td>
              <td>{{ row.duration_ms }}</td>
              <td>{{ row.ip || '-' }}</td>
            </tr>
            <tr v-if="expandedOps[row.id]">
              <td colspan="7" class="detail-row"><pre>{{ JSON.stringify(row.detail ?? row, null, 2) }}</pre></td>
            </tr>
          </template>
        </tbody>
      </table>
      <p v-else class="muted">暂无操作日志。</p>
      <div class="pager" v-if="ops.length">
        <button :disabled="page<=1" @click="page--">上一页</button>
        <span>第 {{ page }} / {{ totalOpsPages() }} 页</span>
        <button :disabled="page>=totalOpsPages()" @click="page++">下一页</button>
      </div>
    </div>

    <div v-else class="card">
      <div class="row row-between">
        <b>AI 用量</b>
        <div class="row compact-row">
          <label>开始</label>
          <input type="datetime-local" v-model="startTime" />
          <label>结束</label>
          <input type="datetime-local" v-model="endTime" />
          <label>每页</label>
          <select v-model.number="perPage">
            <option :value="10">10</option>
            <option :value="20">20</option>
            <option :value="50">50</option>
          </select>
          <button :disabled="busy" @click="loadAi">查询</button>
        </div>
      </div>

      <table class="tbl" v-if="aiLogs.length">
        <thead>
          <tr>
            <th>时间</th>
            <th>详情</th>
            <th>仓库</th>
            <th>模型</th>
            <th>Token 数</th>
            <th>费用(USD)</th>
            <th>耗时(ms)</th>
          </tr>
        </thead>
        <tbody>
          <template v-for="row in aiPageItems()" :key="row.id">
            <tr>
              <td>{{ formatBeijingTime(row.time) }}</td>
              <td>
                <button class="link-btn" @click="toggleAiDetail(row.id)">
                  {{ expandedAi[row.id] ? '收起' : '详情' }}
                </button>
              </td>
              <td>{{ row.repo || '-' }}</td>
              <td>{{ row.model || '-' }}</td>
              <td>{{ row.total_tokens || 0 }}</td>
              <td>{{ formatCost(row.cost_usd) }}</td>
              <td>{{ row.duration_ms || 0 }}</td>
            </tr>
            <tr v-if="expandedAi[row.id]">
              <td colspan="7" class="detail-row"><pre>{{ JSON.stringify(row, null, 2) }}</pre></td>
            </tr>
          </template>
        </tbody>
      </table>
      <p v-else class="muted">暂无 AI 用量日志。</p>
      <div class="pager" v-if="aiLogs.length">
        <button :disabled="page<=1" @click="page--">上一页</button>
        <span>第 {{ page }} / {{ totalAiPages() }} 页</span>
        <button :disabled="page>=totalAiPages()" @click="page++">下一页</button>
      </div>
    </div>
  </section>
</template>

<script>
import { apiGet, ApiError } from '../api/client'

export default {
  name: 'SystemLogsView',
  data() {
    return {
      busy: false,
      err: '',
      tab: 'ops',
      opsStatus: '',
      // pagination / filters
      page: 1,
      perPage: 20,
      startTime: '',
      endTime: '',
      stats: {
        today_request_count: 0,
        today_error_count: 0,
        today_ai_call_count: 0,
      },
      ops: [],
      aiLogs: [],
      expandedOps: {},
      expandedAi: {},
    }
  },
  mounted() {
    this.refreshAll()
  },
  methods: {
    formatErr(e) {
      if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
      if (e instanceof Error) return e.message
      return String(e)
    },
    formatCost(v) {
      const n = Number(v || 0)
      return n.toFixed(6)
    },
    formatBeijingTime(value) {
      if (!value) return '-'
      const raw = String(value).replace(' ', 'T')
      const parsed = new Date(raw.endsWith('Z') ? raw : `${raw}Z`)
      if (Number.isNaN(parsed.getTime())) return String(value)
      return new Intl.DateTimeFormat('zh-CN', {
        timeZone: 'Asia/Shanghai',
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hour12: false,
      }).format(parsed)
    },
    async refreshAll() {
      this.err = ''
      this.busy = true
      try {
        await Promise.all([this.loadStats(), this.loadOps(), this.loadAi()])
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },
    async loadStats() {
      const data = await apiGet('/api/system/logs/stats/today')
      this.stats = {
        today_request_count: data.today_request_count ?? 0,
        today_error_count: data.today_error_count ?? 0,
        today_ai_call_count: data.today_ai_call_count ?? 0,
      }
    },
    async loadOps() {
      this.err = ''
      this.busy = true
      try {
        // request a reasonably large page from server and paginate client-side
        const q = new URLSearchParams()
        q.set('limit', String(this.perPage * 5))
        if (this.opsStatus) q.set('status', this.opsStatus)
        if (this.startTime) q.set('start_time', new Date(this.startTime).toISOString())
        if (this.endTime) q.set('end_time', new Date(this.endTime).toISOString())
        const data = await apiGet(`/api/system/logs/operations?${q.toString()}`)
        this.ops = data.items ?? []
        this.page = 1
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },
    async loadAi() {
      this.err = ''
      this.busy = true
      try {
        const q = new URLSearchParams()
        q.set('limit', String(this.perPage * 5))
        if (this.startTime) q.set('start_time', new Date(this.startTime).toISOString())
        if (this.endTime) q.set('end_time', new Date(this.endTime).toISOString())
        const data = await apiGet(`/api/system/logs/ai-usage?${q.toString()}`)
        this.aiLogs = data.items ?? []
        this.page = 1
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },
    // pagination helpers
    totalOpsPages() {
      return Math.max(1, Math.ceil((this.ops || []).length / this.perPage))
    },
    totalAiPages() {
      return Math.max(1, Math.ceil((this.aiLogs || []).length / this.perPage))
    },
    opsPageItems() {
      const start = (this.page - 1) * this.perPage
      return (this.ops || []).slice(start, start + this.perPage)
    },
    aiPageItems() {
      const start = (this.page - 1) * this.perPage
      return (this.aiLogs || []).slice(start, start + this.perPage)
    },
    toggleOpDetail(id) {
      this.expandedOps = Object.assign({}, this.expandedOps, { [id]: !this.expandedOps[id] })
    },
    toggleAiDetail(id) {
      this.expandedAi = Object.assign({}, this.expandedAi, { [id]: !this.expandedAi[id] })
    },
  },
}
</script>

<style scoped>
.stats-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(180px, 1fr));
  gap: 12px;
  margin: 12px 0;
}

.stat-card {
  background: #ffffff;
  border: 1px solid #d9e3f2;
  border-radius: 10px;
  padding: 12px;
}

.stat-card .label {
  color: #6b778c;
  font-size: 12px;
}

.stat-card .num {
  font-size: 26px;
  font-weight: 700;
  margin-top: 4px;
  color: #1e2f45;
}

.stat-card.danger .num {
  color: #b42318;
}

.stat-card.ai .num {
  color: #175cd3;
}

.tabbar {
  display: flex;
  gap: 8px;
  margin-bottom: 10px;
}

.tab-btn {
  border: 1px solid #c9d8ec;
  background: #fff;
  padding: 7px 12px;
  border-radius: 8px;
  cursor: pointer;
}

.tab-btn.active {
  background: #1f4f82;
  color: #fff;
  border-color: #1f4f82;
}

.pill {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 12px;
  font-size: 12px;
}

.pill.ok {
  background: #eaf7ef;
  color: #067647;
}

.pill.error {
  background: #fdecec;
  color: #b42318;
}

.tbl {
  width: 100%;
  border-collapse: collapse;
}
.tbl th, .tbl td {
  padding: 8px 10px;
  border-bottom: 1px solid #eef3fb;
  text-align: left;
  vertical-align: top;
  font-size: 13px;
}
.link-btn {
  background: transparent;
  border: none;
  color: #175cd3;
  cursor: pointer;
  padding: 0;
}
.detail-row pre {
  margin: 8px 0;
  white-space: pre-wrap;
  word-break: break-word;
  background: #f8fafc;
  padding: 10px;
  border-radius: 6px;
  border: 1px solid #e6eef7;
}
.pager {
  display: flex;
  gap: 8px;
  align-items: center;
  margin-top: 10px;
}
.compact-row label {
  margin-left: 6px;
  margin-right: 6px;
  font-size: 13px;
}

@media (max-width: 900px) {
  .stats-grid {
    grid-template-columns: 1fr;
  }
}
</style>

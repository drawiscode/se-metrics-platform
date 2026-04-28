
<template>
  <section>
    <div class="row">
      <h2>周报 - Repo #{{ repoId }}</h2>
      <button :disabled="busy" @click="loadAll">刷新</button>
      <button :disabled="busy" class="btn-primary" @click="generate">生成周报</button>
      <RouterLink :to="`/repos/${repoId}`">返回仓库</RouterLink>
      <RouterLink to="/repos">返回列表</RouterLink>
    </div>

    <p v-if="err" class="err">{{ err }}</p>

    <div class="card card-wide">
      <div class="row row-between">
        <h3>最新周报</h3>
        <span class="muted" v-if="latest?.created_at">生成于：{{ latest.created_at }}</span>
      </div>

      <div v-if="latest">
        <div class="muted" style="margin: 6px 0 10px;">
          <span v-if="latest.week_start && latest.week_end">周期：{{ latest.week_start }} ~ {{ latest.week_end }}</span>
          <span v-if="latest.model" style="margin-left: 10px;">model：{{ latest.model }}</span>
        </div>

        <!-- 先不引入 Markdown 渲染库：原样展示（可后续再升级渲染） -->
        <pre class="pre report">{{ latest.report_text ?? '' }}</pre>
      </div>
      <p v-else class="muted">暂无周报，点击“生成周报”创建第一份。</p>
    </div>

    <div class="card">
      <div class="row row-between">
        <h3>历史周报</h3>
        <div class="row" style="margin:0;">
          <span class="muted">limit</span>
          <input type="number" v-model.number="limit" min="1" max="50" style="width:90px;" />
          <button :disabled="busy" @click="loadHistory">加载</button>
        </div>
      </div>

      <table v-if="items.length" class="tbl">
        <thead>
          <tr>
            <th>id</th>
            <th>week</th>
            <th>created_at</th>
            <th>model</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="r in items" :key="r.id">
            <td>{{ r.id }}</td>
            <td>{{ r.week_start }} ~ {{ r.week_end }}</td>
            <td>{{ r.created_at }}</td>
            <td>{{ r.model }}</td>
            <td><button :disabled="busy" @click="selectReport(r)">查看</button></td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">暂无历史记录。</p>
    </div>

    <div class="card" v-if="selected">
      <div class="row row-between">
        <h3>查看周报 #{{ selected.id }}</h3>
        <span class="muted">{{ selected.created_at }}</span>
      </div>
      <div class="muted" style="margin: 6px 0 10px;">
        周期：{{ selected.week_start }} ~ {{ selected.week_end }}
        <span v-if="selected.model" style="margin-left: 10px;">model：{{ selected.model }}</span>
      </div>
      <pre class="pre report">{{ selected.report_text ?? '' }}</pre>
    </div>
  </section>
</template>

<script>
import { apiGet, apiPost, ApiError } from '../api/client'

export default {
  name: 'WeeklyReportsView',
  data() {
    return {
      repoId: Number(this.$route.params.id),
      busy: false,
      err: '',
      latest: null,
      items: [],
      selected: null,
      limit: 10,
    }
  },
  mounted() {
    this.loadAll()
  },
  methods: {
    formatErr(e) {
      if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
      if (e instanceof Error) return e.message
      return String(e)
    },

    async loadLatest() {
      try {
        const data = await apiGet(`/api/repos/${this.repoId}/report/latest`)
        this.latest = data
      } catch (e) {
        if (e instanceof ApiError && e.status === 404) {
          this.latest = null
          return
        }
        throw e
      }
    },

    async loadHistory() {
      const lim = Math.max(1, Math.min(50, Number(this.limit) || 10))
      this.limit = lim
      const data = await apiGet(`/api/repos/${this.repoId}/reports?limit=${encodeURIComponent(lim)}`)
      this.items = data.items ?? []
      if (this.items.length && !this.selected) {
        await this.loadReportDetail(this.items[0].id)
      }
    },

    async loadReportDetail(reportId) {
      const data = await apiGet(`/api/repos/${this.repoId}/reports/${reportId}`)
      this.selected = data
    },

    async loadAll() {
      this.err = ''
      this.busy = true
      try {
        await this.loadLatest()
        await this.loadHistory()
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },

    async generate() {
      if (!window.confirm('确认生成周报？这将调用 LLM，可能耗时较长。')) return
      this.err = ''
      this.busy = true
      try {
        const data = await apiPost(`/api/repos/${this.repoId}/report/generate`)
        this.latest = data
        await this.loadHistory()
        this.selected = data
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },

    async selectReport(r) {
      this.err = ''
      this.busy = true
      try {
        await this.loadReportDetail(r.id)
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },
  },
}
</script>

<style scoped>
.card { border:1px solid #e5e7eb; padding:12px; border-radius:8px; background:#fff; margin: 10px 0; }
.row { display:flex; gap:8px; align-items:center; margin: 8px 0 12px; }
.row-between { justify-content: space-between; }
.tbl { width: 100%; border-collapse: collapse; }
.tbl th, .tbl td { border: 1px solid #ddd; padding: 8px; vertical-align: top; }
.err { color: #b00020; white-space: pre-wrap; }
.muted { color: #6b7280; }
.pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; white-space: pre-wrap; }
.report { min-height: 120px; }
.btn-primary { background:rgb(227, 227, 227); color:rgb(28, 33, 42); border:1px solid rgb(162, 165, 170); padding:6px 10px; border-radius:6px; }
</style>
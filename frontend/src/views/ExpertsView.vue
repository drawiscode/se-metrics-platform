<template>
  <section>
    <div class="row">
      <h2>隐形专家 - Repo #{{ repoId }}</h2>
      <button :disabled="busy" @click="loadAll">刷新</button>
      <button :disabled="busy" @click="rebuild">重新计算专家</button>
      <RouterLink :to="`/repos/${repoId}`">返回仓库</RouterLink>
      <RouterLink to="/repos">返回列表</RouterLink>
    </div>

    <p v-if="err" class="err">{{ err }}</p>

    <div class="card">
      <div class="row row-between">
        <h3>全局专家榜（PageRank）</h3>
        <div class="row" style="margin:0;">
          <span class="muted">top</span>
          <input type="number" v-model.number="top" min="1" max="100" style="width:90px;" />
          <button :disabled="busy" @click="loadGlobal">加载</button>
        </div>
      </div>

      <table class="tbl" v-if="experts.length">
        <thead>
          <tr>
            <th>#</th>
            <th>login</th>
            <th>primary_module</th>
            <th>commit_count</th>
            <th>files_touched</th>
            <th>last_active</th>
            <th>pagerank</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(e, idx) in experts" :key="e.login + ':' + idx">
            <td>{{ idx + 1 }}</td>
            <td>{{ e.login }}</td>
            <td>{{ e.primary_module || '-' }}</td>
            <td>{{ e.commit_count ?? 0 }}</td>
            <td>{{ e.files_touched ?? 0 }}</td>
            <td>{{ e.last_active || '-' }}</td>
            <td>{{ formatScore(e.pagerank) }}</td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">暂无专家数据（可能需要先同步 commit_files / PR 数据，或点击“重新计算专家”）。</p>
    </div>

    <div class="card">
      <div class="row row-between">
        <h3>模块专家（按目录）</h3>
        <div class="row" style="margin:0;">
          <span class="muted">dir</span>
          <input v-model="dir" placeholder="例如 src/ai 或 backend/src/ai" style="width:280px;" />
          <span class="muted">top</span>
          <input type="number" v-model.number="moduleTop" min="1" max="50" style="width:90px;" />
          <button :disabled="busy || !dir.trim()" @click="loadModule">查询</button>
        </div>
      </div>

      <table class="tbl" v-if="moduleExperts.length">
        <thead>
          <tr>
            <th>#</th>
            <th>login</th>
            <th>commit_count</th>
            <th>lines_changed</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(m, idx) in moduleExperts" :key="m.login + ':' + idx">
            <td>{{ idx + 1 }}</td>
            <td>{{ m.login }}</td>
            <td>{{ m.commit_count ?? 0 }}</td>
            <td>{{ m.lines_changed ?? 0 }}</td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">输入目录后点击“查询”。</p>
    </div>

    <div class="card" v-if="buildInfo">
      <h3>最近一次重算结果</h3>
      <pre class="pre">{{ buildInfo }}</pre>
    </div>
  </section>
</template>

<script>
import { apiGet, apiPost, ApiError } from '../api/client'

export default {
  name: 'ExpertsView',
  props: {
    id: { type: String, required: true },
  },
  data() {
    return {
      busy: false,
      err: '',

      top: 20,
      experts: [],

      dir: '',
      moduleTop: 10,
      moduleExperts: [],

      buildInfo: '',
    }
  },
  computed: {
    repoId() {
      return Number(this.id)
    },
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

    formatScore(v) {
      const n = Number(v ?? 0)
      if (!Number.isFinite(n)) return '-'
      return n.toFixed(4)
    },

    async loadGlobal() {
      const t = Math.max(1, Math.min(100, Number(this.top) || 20))
      this.top = t
      const data = await apiGet(`/api/repos/${this.repoId}/experts?top=${encodeURIComponent(t)}`)
      this.experts = data.items ?? []
    },

    async loadModule() {
      const d = String(this.dir || '').trim()
      if (!d) {
        this.moduleExperts = []
        return
      }
      const t = Math.max(1, Math.min(50, Number(this.moduleTop) || 10))
      this.moduleTop = t
      const data = await apiGet(
        `/api/repos/${this.repoId}/experts/module?dir=${encodeURIComponent(d)}&top=${encodeURIComponent(t)}`,
      )
      this.moduleExperts = data.items ?? []
    },

    async rebuild() {
      if (!window.confirm('确认重新计算专家？这可能耗时较长。')) return
      this.err = ''
      this.busy = true
      try {
        const data = await apiPost(`/api/repos/${this.repoId}/experts/build`)
        this.buildInfo = JSON.stringify(data, null, 2)
        await this.loadGlobal()
        if (String(this.dir || '').trim()) {
          await this.loadModule()
        }
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },

    async loadAll() {
      this.err = ''
      this.busy = true
      try {
        await this.loadGlobal()
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
</style>

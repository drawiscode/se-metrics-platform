<template>
  <section>
    <div class="row">
      <h2>Repo #{{ repoId }}</h2>
      <button :disabled="busy" @click="loadAll">刷新</button>
      <RouterLink to="/repos">返回列表</RouterLink>
    </div>

    <p v-if="err" class="err">{{ err }}</p>

    <div class="grid">
      <div class="card">
        <h3>Metrics</h3>
        <pre class="pre">{{ metricsText }}</pre>
      </div>

      <div class="card">
        <h3>Health</h3>
        <pre class="pre">{{ healthText }}</pre>
      </div>

      <div class="card">
        <h3>Activity (days={{ days }})</h3>
        <div class="row">
          <input type="number" min="1" v-model.number="days" />
          <button :disabled="busy" @click="loadActivity">加载</button>
        </div>
        <table class="tbl" v-if="activity.length">
          <thead><tr><th>date</th><th>commits</th></tr></thead>
          <tbody>
            <tr v-for="a in activity" :key="a.date">
              <td>{{ a.date }}</td>
              <td>{{ a.commits }}</td>
            </tr>
          </tbody>
        </table>
      </div>

      <div class="card">
        <h3>Hotfiles</h3>
        <table class="tbl" v-if="hotfiles.length">
          <thead><tr><th>filename</th><th>commits</th><th>+add</th><th>-del</th></tr></thead>
          <tbody>
            <tr v-for="f in hotfiles" :key="f.filename">
              <td>{{ f.filename }}</td>
              <td>{{ f.commits }}</td>
              <td>{{ f.additions }}</td>
              <td>{{ f.deletions }}</td>
            </tr>
          </tbody>
        </table>
      </div>

      <div class="card">
        <h3>Hotdirs</h3>
        <table class="tbl" v-if="hotdirs.length">
          <thead><tr><th>dir</th><th>commits</th><th>+add</th><th>-del</th></tr></thead>
          <tbody>
            <tr v-for="d in hotdirs" :key="d.dir ?? d.dirname ?? ''">
              <td>{{ d.dir ?? d.dirname }}</td>
              <td>{{ d.commits }}</td>
              <td>{{ d.additions }}</td>
              <td>{{ d.deletions }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </section>
</template>

<script>
    import { apiGet, ApiError } from '../api/client'

    export default {
        name: 'RepoDetailView',
        props: {
            id: { type: String, required: true },
        },
        data() {
            return {
                busy: false,
                err: '',

                metrics: null,
                health: null,

                days: 30,
                activity: [],
                hotfiles: [],
                hotdirs: [],
            }
        },
        computed: {
            repoId() {
            return Number(this.id)
            },
            metricsText() {
            return this.metrics ? JSON.stringify(this.metrics, null, 2) : ''
            },
            healthText() {
            return this.health ? JSON.stringify(this.health, null, 2) : ''
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

            async loadAll() {
                this.err = ''
                this.busy = true
                try {
                    this.metrics = await apiGet(`/api/repos/${this.repoId}/metrics`)
                    this.health = await apiGet(`/api/repos/${this.repoId}/health`)
                    await this.loadActivity()

                    const hf = await apiGet(`/api/repos/${this.repoId}/hotfiles`)
                    this.hotfiles = hf.items ?? hf

                    const hd = await apiGet(`/api/repos/${this.repoId}/hotdirs`)
                    this.hotdirs = hd.items ?? hd
                } catch (e) {
                    this.err = this.formatErr(e)
                } finally {
                    this.busy = false
                }
            },

            async loadActivity() {
                const data = await apiGet(`/api/repos/${this.repoId}/activity?days=${this.days}`)
                this.activity = data.items ?? data
            },
        },
    }
</script>

<style scoped>
    .row { display:flex; gap:12px; align-items:center; margin-bottom: 12px; }
    .grid { display:grid; grid-template-columns: 1fr 1fr; gap:12px; }
    .card { border:1px solid #e5e7eb; padding:12px; border-radius:8px; background:#fff; }
    .tbl { border-collapse: collapse; width: 100%; }
    .tbl th, .tbl td { border: 1px solid #ddd; padding: 6px 8px; }
    .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; max-height: 320px; }
    .err { color: #b00020; white-space: pre-wrap; }
    input[type="number"] { width: 120px; padding: 4px 6px; }
</style>
<template>
  <section>
    <div class="row">
      <h2>Repo #{{ repoId }}</h2>
      <button :disabled="busy" @click="loadAll">刷新</button>
      <RouterLink :to="`/repos/${repoId}/tasks`">任务清单</RouterLink>
      <RouterLink :to="`/repos/${repoId}/experts`">隐形专家</RouterLink>
      <RouterLink to="/repos">返回列表</RouterLink>
    </div>

    <p v-if="err" class="err">{{ err }}</p>

    <div class="grid">
      <div class="card card-wide">
        <div class="row row-between">
          <h3>仓库介绍</h3>
          <span class="muted" v-if="introUpdatedAt">更新于：{{ introUpdatedAt }}</span>
        </div>
        <p v-if="introText" class="pre intro">{{ introText }}</p>
        <p v-else class="muted">暂无介绍（首次同步成功后会自动生成）。</p>
      </div>

      <div class="card">
        <h3>Metrics</h3>
        <pre class="pre">{{ metricsText }}</pre>
      </div>

      <div class="card">
        <h3>Health</h3>
        <pre class="pre">{{ healthText }}</pre>
      </div>

      <div class="card">
        <h3>CI Health</h3>
        <div class="ci-head" v-if="ciHealth">
          <span class="pill" :class="levelClass(ciHealth.health_level)">{{ ciHealth.health_level }}</span>
          <strong class="score">Score: {{ Number(ciHealth.score ?? 0).toFixed(1) }}</strong>
        </div>
        <table class="tbl" v-if="ciHealth">
          <tbody>
            <tr>
              <td>24h Completed</td>
              <td>{{ ciHealth.completed_24h ?? 0 }}</td>
            </tr>
            <tr>
              <td>24h Failed</td>
              <td>{{ ciHealth.failed_24h ?? 0 }}</td>
            </tr>
            <tr>
              <td>24h Failure Rate</td>
              <td>{{ formatPercent(ciHealth.failure_rate_24h) }}</td>
            </tr>
            <tr>
              <td>Consecutive Failures</td>
              <td>{{ ciHealth.consecutive_failures ?? 0 }}</td>
            </tr>
            <tr>
              <td>Avg Duration (7d)</td>
              <td>{{ Number(ciHealth.avg_duration_hours_7d ?? 0).toFixed(2) }} h</td>
            </tr>
            <tr>
              <td>Latest Run</td>
              <td>{{ ciHealth.latest_run_at || '-' }}</td>
            </tr>
          </tbody>
        </table>
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

      <div class="card card-wide">
        <div class="row row-between">
          <h3>Recent CI Runs</h3>
          <button :disabled="busy" @click="loadCiRuns">刷新 CI</button>
        </div>
        <table class="tbl" v-if="ciRuns.length">
          <thead>
            <tr>
              <th>run_id</th>
              <th>workflow</th>
              <th>status</th>
              <th>conclusion</th>
              <th>created_at</th>
              <th>actor</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="r in ciRuns" :key="r.run_id">
              <td>
                <a v-if="r.html_url" :href="r.html_url" target="_blank" rel="noreferrer">{{ r.run_id }}</a>
                <span v-else>{{ r.run_id }}</span>
              </td>
              <td>{{ r.name || '-' }}</td>
              <td>{{ r.status || '-' }}</td>
              <td>{{ r.conclusion || '-' }}</td>
              <td>{{ r.created_at || '-' }}</td>
              <td>{{ r.actor_login || '-' }}</td>
            </tr>
          </tbody>
        </table>
        <p v-else>暂无 CI 运行数据，可先在仓库列表点击 sync。</p>
      </div>

      <div class="card card-wide">
        <div class="row row-between">
          <h3>CI Failure Trend</h3>
          <div class="row compact-row">
            <label for="trendDays">days</label>
            <input id="trendDays" type="number" min="1" max="60" v-model.number="ciTrendDays" />
            <button :disabled="busy" @click="loadCiTrend">加载趋势</button>
          </div>
        </div>

        <div class="trend-wrap" v-if="ciTrend.length">
          <svg viewBox="0 0 520 180" class="trend-svg" role="img" aria-label="CI failure rate trend">
            <line x1="28" y1="20" x2="28" y2="156" class="axis" />
            <line x1="28" y1="156" x2="500" y2="156" class="axis" />
            <polyline :points="ciTrendPoints" class="trend-line" />
            <g v-for="(p, idx) in ciTrendPointObjects" :key="`${p.date}-${idx}`">
              <circle :cx="p.x" :cy="p.y" r="3.5" class="trend-dot" />
            </g>
          </svg>
        </div>

        <table class="tbl" v-if="ciTrend.length">
          <thead>
            <tr>
              <th>date</th>
              <th>completed</th>
              <th>failed</th>
              <th>failure_rate</th>
              <th>avg_duration_hours</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="t in ciTrend" :key="t.date">
              <td>{{ t.date }}</td>
              <td>{{ t.completed ?? 0 }}</td>
              <td>{{ t.failed ?? 0 }}</td>
              <td>{{ formatPercent(t.failure_rate) }}</td>
              <td>{{ Number(t.avg_duration_hours ?? 0).toFixed(2) }}</td>
            </tr>
          </tbody>
        </table>
        <p v-else>暂无趋势数据。</p>
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

                introText: '',
                introUpdatedAt: '',

                metrics: null,
                health: null,
                ciHealth: null,
                ciRuns: [],
                ciTrendDays: 7,
                ciTrend: [],

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
            ciTrendPointObjects() {
              if (!this.ciTrend.length) return []

              const width = 520
              const height = 180
              const left = 28
              const right = 20
              const top = 20
              const bottom = 24

              const rates = this.ciTrend.map((t) => Number(t.failure_rate ?? 0))
              const maxRate = Math.max(0.05, ...rates)
              const n = this.ciTrend.length
              const innerW = width - left - right
              const innerH = height - top - bottom

              return this.ciTrend.map((t, idx) => {
                const x = n <= 1 ? left + innerW / 2 : left + (idx * innerW) / (n - 1)
                const y = top + (1 - Number(t.failure_rate ?? 0) / maxRate) * innerH
                return { x, y, date: t.date }
              })
            },
            ciTrendPoints() {
              return this.ciTrendPointObjects.map((p) => `${p.x},${p.y}`).join(' ')
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
                    this.ciHealth = await apiGet(`/api/repos/${this.repoId}/ci/health`)
                    await this.loadCiRuns()
                    await this.loadCiTrend()
                    await this.loadActivity()
                    await this.loadIntro()

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
            async loadIntro(){
                const data = await apiGet(`/api/repos/${this.repoId}/intro`)
                this.introText = data.intro_text ?? ''
                this.introUpdatedAt = data.intro_updated_at ?? ''
            },

            async loadActivity() {
                const data = await apiGet(`/api/repos/${this.repoId}/activity?days=${this.days}`)
                this.activity = data.items ?? data
            },

            async loadCiRuns() {
              const data = await apiGet(`/api/repos/${this.repoId}/ci/runs?limit=8`)
              this.ciRuns = data.items ?? data
            },

            async loadCiTrend() {
              const d = Math.max(1, Math.min(60, Number(this.ciTrendDays || 7)))
              this.ciTrendDays = d
              const data = await apiGet(`/api/repos/${this.repoId}/ci/trend?days=${d}`)
              this.ciTrend = data.items ?? []
            },

            formatPercent(v) {
              const n = Number(v ?? 0)
              return `${(n * 100).toFixed(1)}%`
            },

            levelClass(level) {
              if (level === 'critical') return 'is-critical'
              if (level === 'warning') return 'is-warning'
              return 'is-healthy'
            },
        },
    }
</script>

<style scoped>
    .pre.intro { white-space: pre-wrap; max-height: none; }
    .muted { color: #6b7280; }

    .row { display:flex; gap:12px; align-items:center; margin-bottom: 12px; }
        .row-between { justify-content: space-between; }
    .grid { display:grid; grid-template-columns: 1fr 1fr; gap:12px; }
    .card { border:1px solid #e5e7eb; padding:12px; border-radius:8px; background:#fff; }
        .card-wide { grid-column: 1 / -1; }
    .tbl { border-collapse: collapse; width: 100%; }
    .tbl th, .tbl td { border: 1px solid #ddd; padding: 6px 8px; }
    .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; max-height: 320px; }
    .err { color: #b00020; white-space: pre-wrap; }
    input[type="number"] { width: 120px; padding: 4px 6px; }
        .ci-head { display:flex; align-items:center; gap:10px; margin-bottom:8px; }
        .score { font-size: 16px; }
        .pill {
          display:inline-block;
          border-radius: 999px;
          padding: 2px 10px;
          font-size: 12px;
          font-weight: 600;
          text-transform: uppercase;
          letter-spacing: .04em;
        }
        .is-healthy { background:#d1fae5; color:#065f46; }
        .is-warning { background:#fef3c7; color:#92400e; }
        .is-critical { background:#fee2e2; color:#991b1b; }
        .compact-row { gap: 8px; margin-bottom: 0; }
        .trend-wrap {
          border: 1px solid #e5e7eb;
          border-radius: 8px;
          padding: 8px;
          background: #fcfcfd;
          margin-bottom: 10px;
          overflow-x: auto;
        }
        .trend-svg { width: 100%; min-width: 500px; height: 180px; display: block; }
        .axis { stroke: #cbd5e1; stroke-width: 1; }
        .trend-line {
          fill: none;
          stroke: #0f766e;
          stroke-width: 2.5;
          stroke-linecap: round;
          stroke-linejoin: round;
        }
        .trend-dot { fill: #0f766e; }

        @media (max-width: 900px) {
          .grid { grid-template-columns: 1fr; }
          .card-wide { grid-column: auto; }
        }
</style>
<template>
  <section>
    <div class="row row-between">
      <h2>仓库列表</h2>
      <button type="button" :disabled="busy" @click="load">刷新</button>
    </div>

    <div class="card">
      <div class="row row-between">
        <b>自动化闭环</b>
        <label class="row muted" style="gap:6px; margin:0;">
          <input type="checkbox" v-model="autoGenTasksAfterSync" />
          同步后自动生成任务清单并跳转（暂时禁用，防止测试过慢）
        </label>
      </div>
      <p class="muted" style="margin:6px 0 0;">
        说明：将调用 AI 生成 JSON 任务并持久化（当前已禁用）。
      </p>
    </div>

    <div class="card tip">
      <b>使用建议：</b>
      <ol>
        <li>添加仓库(owner/repo)</li>
        <li>先点一次「Sync(全量)」拉取基础数据</li>
        <li>之后日常用「Sync(增量)」即可</li>
      </ol>
    </div>

    <div class="card">
      <div class="row row-between">
        <b>同步参数</b>
      </div>

      <div class="row">
        <span class="muted" style="min-width: 110px;">Issues</span>
        <label class="muted">start <input type="number" v-model.number="syncFullParams.issues_page_start" style="width:90px;" /></label>
        <label class="muted">pages <input type="number" v-model.number="syncFullParams.issues_pages_count" style="width:90px;" /></label>
        <label class="muted">增量上限 <input type="number" v-model.number="syncIncrParams.issues_pages_count" style="width:90px;" /></label>
      </div>

      <div class="row">
        <span class="muted" style="min-width: 110px;">Pulls</span>
        <label class="muted">start <input type="number" v-model.number="syncFullParams.pulls_page_start" style="width:90px;" /></label>
        <label class="muted">pages <input type="number" v-model.number="syncFullParams.pulls_pages_count" style="width:90px;" /></label>
        <label class="muted">增量上限 <input type="number" v-model.number="syncIncrParams.pulls_pages_count" style="width:90px;" /></label>
      </div>

      <div class="row">
        <span class="muted" style="min-width: 110px;">Commits</span>
        <label class="muted">start <input type="number" v-model.number="syncFullParams.commits_page_start" style="width:90px;" /></label>
        <label class="muted">pages <input type="number" v-model.number="syncFullParams.commits_pages_count" style="width:90px;" /></label>
        <label class="muted">增量上限 <input type="number" v-model.number="syncIncrParams.commits_pages_count" style="width:90px;" /></label>
      </div>

      <div class="row">
        <span class="muted" style="min-width: 110px;">Releases</span>
        <label class="muted">start <input type="number" v-model.number="syncFullParams.releases_page_start" style="width:90px;" /></label>
        <label class="muted">pages <input type="number" v-model.number="syncFullParams.releases_pages_count" style="width:90px;" /></label>
        <label class="muted">增量上限 <input type="number" v-model.number="syncIncrParams.releases_pages_count" style="width:90px;" /></label>
      </div>

      <div class="row">
        <span class="muted" style="min-width: 110px;">CI</span>
        <label class="muted">pages <input type="number" v-model.number="syncFullParams.ci_pages_count" style="width:90px;" /></label>
        <label class="muted">增量上限 <input type="number" v-model.number="syncIncrParams.ci_pages_count" style="width:90px;" /></label>
      </div>

      <p class="muted" style="margin:6px 0 0;">
        说明：后端 incremental 会忽律参数
      </p>
    </div>



    <form class="row" @submit.prevent="createRepo">
      <input v-model="fullName" placeholder="owner/repo" />
      <button :disabled="busy || !fullName.trim()">添加</button>
    </form>

    <p v-if="busy" class="muted">处理中...</p>
    <p v-if="err" class="err">{{ err }}</p>

    <table class="tbl" v-if="items.length">
      <thead>
        <tr>
          <th>id</th>
          <th>full_name</th>
          <th>enabled</th>
          <th>快捷</th>
          <th>操作</th>
        </tr>
      </thead>

      <tbody>
        <tr v-for="r in items" :key="r.id">
          <td>{{ r.id }}</td>
          <td>
            <RouterLink :to="`/repos/${r.id}`">{{ r.full_name }}</RouterLink>
          </td>
          <td>{{ r.enabled }}</td>

          <td class="ops">
            <RouterLink class="btn-link" :to="`/ai?repo_id=${r.id}`">AI</RouterLink>
            <RouterLink class="btn-link" :to="`/repos/${r.id}/tasks`">任务清单</RouterLink>
          </td>

          <td class="ops">
            <button :disabled="busy" @click="syncIncremental(r.id)">Sync(增量)</button>
            <button :disabled="busy" @click="syncFull(r.id)">Sync(全量)</button>
            <button :disabled="busy" @click="sync_commit_files(r.id)">Sync commit_files(30)</button>
            <button class="btn-danger" :disabled="busy" @click="deleteRepo(r.id, r.full_name)">删除</button>
          </td>
        </tr>
      </tbody>
    </table>

    <p v-else class="muted">暂无仓库，请先添加。</p>

    <div v-if="syncSummary" class="card okbox">
      <b>最近一次操作结果</b>
      <p class="ok">{{ syncSummary }}</p>
      <pre v-if="lastSync" class="pre">{{ lastSync }}</pre>
    </div>
  </section>
</template>

<script>
  import { apiGet, apiPost, apiDelete, ApiError } from '../api/client'

  export default {
    name: 'RepoView',
    data() {
      return {
        items: [],
        fullName: '',
        busy: false,
        err: '',
        syncSummary: '',
        lastSync: '',

        // 自动生成任务：禁用
        autoGenTasksAfterSync: false,//false 已禁用
        lastAiAnswer: '', // 新增：调试 AI 原始输出

        // 同步参数
        syncFullParams: {
          issues_page_start: 1,
          issues_pages_count: 1,
          pulls_page_start: 1,
          pulls_pages_count: 1,
          commits_page_start: 1,
          commits_pages_count: 1,
          releases_page_start: 1,
          releases_pages_count: 1,
          ci_pages_count: 2,
        },
        syncIncrParams: {
          issues_pages_count: 50,
          pulls_pages_count: 50,
          commits_pages_count: 50,
          releases_pages_count: 50,
          ci_pages_count: 3,
        },
      }
    },
    mounted() {
      this.load()
    },
    methods: {
      buildSyncQuery(mode) {
        const p = new URLSearchParams()
        p.set('mode', mode)

        const clampPosInt = (v, defv) => {
          const n = Number(v)
          if (!Number.isFinite(n)) return defv
          return Math.max(1, Math.floor(n))
        }

        if (mode === 'full') {
          const f = this.syncFullParams
          p.set('issues_page_start', String(clampPosInt(f.issues_page_start, 1)))
          p.set('issues_pages_count', String(clampPosInt(f.issues_pages_count, 1)))

          p.set('pulls_page_start', String(clampPosInt(f.pulls_page_start, 1)))
          p.set('pulls_pages_count', String(clampPosInt(f.pulls_pages_count, 1)))

          p.set('commits_page_start', String(clampPosInt(f.commits_page_start, 1)))
          p.set('commits_pages_count', String(clampPosInt(f.commits_pages_count, 1)))

          p.set('releases_page_start', String(clampPosInt(f.releases_page_start, 1)))
          p.set('releases_pages_count', String(clampPosInt(f.releases_pages_count, 1)))

          p.set('ci_pages_count', String(clampPosInt(f.ci_pages_count, 2)))
        } else {
          const i = this.syncIncrParams
          p.set('issues_pages_count', String(clampPosInt(i.issues_pages_count, 50)))
          p.set('pulls_pages_count', String(clampPosInt(i.pulls_pages_count, 50)))
          p.set('commits_pages_count', String(clampPosInt(i.commits_pages_count, 50)))
          p.set('releases_pages_count', String(clampPosInt(i.releases_pages_count, 50)))
          p.set('ci_pages_count', String(clampPosInt(i.ci_pages_count, 3)))
        }

        const qs = p.toString()
        return qs ? `?${qs}` : ''
      },

      async deleteRepo(id, fullName) {
        if (!window.confirm(`确认删除仓库吗？\n${fullName} (id=${id})`)) return
        this.err = ''
        this.busy = true
        try {
          await apiDelete(`/api/repos/${id}`)
          await this.load()
          this.syncSummary = '删除成功'
          this.lastSync = ''
        } catch (e) {
          this.err = this.formatErr(e)
        } finally {
          this.busy = false
        }
      },
      formatErr(e) {
        if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
        if (e instanceof Error) return e.message
        return String(e)
      },

      setSyncResult(data) {
        this.lastSync = JSON.stringify(data, null, 2)

        if (data && data.ok && typeof data.kb_total_indexed !== 'undefined') {
          this.syncSummary =
            `同步成功：本次构建知识块 ${data.kb_total_indexed} 条 ` +
            `(Issue ${data.kb_issues_indexed ?? 0} / PR ${data.kb_pulls_indexed ?? 0} / ` +
            `Commit ${data.kb_commits_indexed ?? 0} / Release ${data.kb_releases_indexed ?? 0})`

          if (typeof data.ci_runs_upserted !== 'undefined') {
            this.syncSummary +=
              `\nCI：新增/更新 ${data.ci_runs_upserted} 条运行记录，` +
              `24h失败率 ${(Number(data.ci_failure_rate_24h ?? 0) * 100).toFixed(1)}%，` +
              `连续失败 ${data.ci_consecutive_failures ?? 0} 次，` +
              `新增CI告警 ${data.ci_alerts_created ?? 0} 条`
          }
        } else if (data && data.ok) {
          this.syncSummary = '同步成功'
        } else {
          this.syncSummary = ''
        }
      },

      async load() {
        this.err = ''
        this.busy = true
        try {
          const data = await apiGet('/api/repos')
          this.items = data.items ?? data
        } catch (e) {
          this.err = this.formatErr(e)
        } finally {
          this.busy = false
        }
      },

      async createRepo() {
        this.err = ''
        this.busy = true
        try {
          const name = encodeURIComponent(this.fullName.trim())
          await apiPost(`/api/repos?full_name=${name}`)
          this.fullName = ''
          await this.load()
        } catch (e) {
          this.err = this.formatErr(e)
        } finally {
          this.busy = false
        }
      },

      buildAiTasksPrompt(repoId) {
        return [
              "请为该仓库生成【任务清单 JSON】（用于导入任务系统）。",
              "要求：",
              "1) 输出必须是严格 JSON，不要 Markdown。",
              "2) 顶层格式：{ \"tasks\": [ ... ] }",
              "3) 每个任务字段：title(string), priority('P0'|'P1'|'P2'), reason(string), actions(array of string), expected_benefit(string), verify(string).",
              "4) 任务总数 5-10 条，按优先级覆盖 P0/P1/P2。",
              "5) 要基于仓库数据（metrics/health、issues/PRs/commits、CI、风险告警）给出可执行任务；证据不足时给出补采集/补监控任务。",
        ].join("\n")
      },

      extractJsonObject(text) {
        if (!text) return null
        const s = String(text).trim()

        // 1) ```json fenced block
        const fenced = s.match(/```json\s*([\s\S]*?)\s*```/i)
        if (fenced && fenced[1]) return fenced[1].trim()

        // 2) balanced {...}
        const start = s.indexOf('{')
        if (start >= 0) {
          let depth = 0
          for (let i = start; i < s.length; i++) {
            const ch = s[i]
            if (ch === '{') depth++
            else if (ch === '}') {
              depth--
              if (depth === 0) return s.slice(start, i + 1)
            }
          }
        }

        // 3) balanced [...]
        const as = s.indexOf('[')
        if (as >= 0) {
          let depth = 0
          for (let i = as; i < s.length; i++) {
            const ch = s[i]
            if (ch === '[') depth++
            else if (ch === ']') {
              depth--
              if (depth === 0) return s.slice(as, i + 1)
            }
          }
        }

        return null
      },
      parseAiTasksFromAnswer(answerText) {
        const jsonStr = this.extractJsonObject(answerText)
        if (!jsonStr) throw new Error('AI 输出中未找到可解析的 JSON。')

        let payload = null
        try { payload = JSON.parse(jsonStr) } catch (_) { payload = null }

        const tasks =
          (payload && Array.isArray(payload.tasks) && payload.tasks) ||
          (Array.isArray(payload) && payload) ||
          null

        if (!tasks) throw new Error('AI 返回不是有效 JSON（缺少 tasks 数组）。')
        return tasks
      },

      async generateTasksAndPersist(repoId) {
        // 调 AI ask
        const res = await fetch('/api/ai/ask', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            repo_id: Number(repoId),
            question: this.buildAiTasksPrompt(repoId),
          }),
        })

        const text = await res.text()
        if (!res.ok) throw new ApiError(res.status, 'POST /api/ai/ask failed', text)

        const data = text ? JSON.parse(text) : {}
        if (data.success === false) throw new Error(data.error || 'AI failed')

        // ✅ 关键：先保存 answer
        this.lastAiAnswer = data.answer ?? ''

        //  提取 + 解析
        const tasks = this.parseAiTasksFromAnswer(this.lastAiAnswer)
        await apiPost(`/api/repos/${repoId}/tasks`, { items: tasks })
      },

      async syncIncremental(id) {
        this.err = ''
        this.busy = true
        this.syncSummary = ''
        this.lastSync = ''
        try {
          const url = `/api/repos/${id}/sync${this.buildSyncQuery('incremental')}`
          const data = await apiPost(url)
          this.setSyncResult(data)

          //先禁用
          //if (this.autoGenTasksAfterSync) {
            //try {
              //await this.generateTasksAndPersist(id)
              //this.$router.push(`/repos/${id}/tasks`)
            //} catch (e) {
              //this.err = `同步成功，但任务清单生成失败：\n${this.formatErr(e)}`
              //if (this.lastAiAnswer) {
                //this.lastSync =
               //(this.lastSync ? this.lastSync + '\n\n' : '') +
                 //'--- AI answer ---\n' +
                  //this.lastAiAnswer
              //}
            //}
          //}
        } catch (e) {
          this.err = this.formatErr(e)
        } finally {
          this.busy = false
        }
      },

      async syncFull(id) {
          this.err = ''
          this.busy = true
          this.syncSummary = ''
          this.lastSync = ''
          try {
            const url = `/api/repos/${id}/sync${this.buildSyncQuery('full')}`
            const data = await apiPost(url)
            this.setSyncResult(data)
            
            //暂时禁用
            //if (this.autoGenTasksAfterSync) {
              //try {
                //await this.generateTasksAndPersist(id)
                //this.$router.push(`/repos/${id}/tasks`)
              //} catch (e) {
                //this.err = `同步成功，但任务清单生成失败：\n${this.formatErr(e)}`
                //if (this.lastAiAnswer) {
                  //this.lastSync =
                    //(this.lastSync ? this.lastSync + '\n\n' : '') +
                    //'--- AI answer ---\n' +
                    //this.lastAiAnswer
                //}
              //}
            //}
          } catch (e) {
            this.err = this.formatErr(e)
          } finally {
            this.busy = false
          }
      },

      async sync_commit_files(id) {
        this.err = ''
        this.busy = true
        this.syncSummary = ''
        this.lastSync = ''
        try {
          const data = await apiPost(`/api/repos/${id}/sync/commit_files?limit=30`)
          this.lastSync = JSON.stringify(data, null, 2)
          this.syncSummary = data && data.ok ? '同步 commit_files 成功' : ''
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
  input { width: 320px; padding: 6px 8px; }
  .tbl { border-collapse: collapse; width: 100%; }
  .tbl th, .tbl td { border: 1px solid #ddd; padding: 8px; vertical-align: top; }
  .ops { display:flex; gap:8px; flex-wrap: wrap; align-items: center; }
  .err { color: #b00020; white-space: pre-wrap; }
  .ok { color: #0f7b3c; white-space: pre-wrap; margin: 10px 0 0; }
  .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; }
  .muted { color: #6b7280; }
  .card { border:1px solid #e5e7eb; padding:12px; border-radius:8px; background:#fff; margin: 10px 0; }
  .tip ol { margin: 6px 0 0 18px; }
  .okbox b { display:block; margin-bottom: 6px; }
  .btn-link {
    display:inline-block;
    padding: 4px 10px;
    border: 1px solid #e5e7eb;
    border-radius: 6px;
    text-decoration: none;
  }
</style>
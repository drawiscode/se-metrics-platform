<template>
  <section>
    <div class="row row-between">
      <div class="row">
        <h2>任务清单(Repo #{{ repoId }})</h2>
        <RouterLink :to="`/repos/${repoId}`">返回仓库详情</RouterLink>
        <RouterLink to="/repos">返回列表</RouterLink>
      </div>

      <div class="row">
        <select v-model="filterStatus">
          <option value="">全部</option>
          <option value="open">未完成</option>
          <option value="done">已完成</option>
        </select>
        <button :disabled="busy" @click="load">刷新</button>
      </div>
    </div>

    <p v-if="err" class="err">{{ err }}</p>
    <p v-if="busy" class="muted">加载中...</p>

    <div class="card">
      <div class="row row-between">
        <h3>AI 生成</h3>
        <button :disabled="busy || genBusy" @click="generateFromAi">生成/更新任务清单</button>
      </div>
      <p v-if="genErr" class="err">{{ genErr }}</p>


      <p class="muted">
        说明：生成会写入持久化任务；同标题会去重（已完成任务不会被覆盖）。
      </p>
    </div>

      <table class="tbl" v-if="items.length">
        <thead>
          <tr>
            <th>priority</th>
            <th>title</th>
            <th>status</th>
            <th>reason</th>
            <th>actions</th>
            <th>verify</th>
            <th>op</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="t in items" :key="t.id">
            <td>
              <template v-if="editingId === t.id">
                <select v-model="editForm.priority" class="ctl">
                  <option value="P0">P0</option>
                  <option value="P1">P1</option>
                  <option value="P2">P2</option>
                </select>
              </template>
              <template v-else>
                <span class="pill" :class="pillClass(t.priority)">{{ t.priority }}</span>
              </template>
            </td>

            <td class="title">
              <template v-if="editingId === t.id">
                <input v-model="editForm.title" class="ctl ctl-title" />
              </template>
              <template v-else>
                {{ t.title }}
              </template>
            </td>

            <td>{{ t.status }}</td>

            <td class="wrap">
              <textarea v-if="editingId === t.id" v-model="editForm.reason" class="ta"></textarea>
              <span v-else>{{ t.reason }}</span>
            </td>

            <td class="wrap">
              <template v-if="editingId === t.id">
                <textarea v-model="editForm.actionsText" class="ta" placeholder="每行一条 action"></textarea>
              </template>
              <template v-else>
                <ol v-if="parsedActions(t).length" class="actions-list">
                  <li v-for="(a, idx) in parsedActions(t)" :key="idx" class="action-item" >{{ a }}</li>
                </ol>
                <span v-else>-</span>
              </template>
            </td>

            <td class="wrap">
              <textarea v-if="editingId === t.id" v-model="editForm.verify" class="ta"></textarea>
              <span v-else>{{ t.verify || '-' }}</span>
            </td>

            <td class="ops">
              <div class="ops-inner">
                <template v-if="editingId === t.id">
                  <button :disabled="busy" class="btn" @click="saveEdit(t.id)">保存</button>
                  <button :disabled="busy" class="btn" @click="cancelEdit">取消</button>
                </template>
                <template v-else>
                  <button :disabled="busy" class="btn" @click="startEdit(t)">编辑</button>
                  <button v-if="t.status !== 'done'" :disabled="busy" class="btn" @click="markDone(t.id)">完成</button>
                  <button v-else :disabled="busy" class="btn" @click="reopen(t.id)">恢复</button>
                  <button :disabled="busy" class="btn btn-danger" @click="remove(t.id)">删除</button>
                </template>
              </div>
            </td>
          </tr>
        </tbody>
      </table>

    <p v-else class="muted">暂无任务。点击上方“生成/更新任务清单”。</p>
  </section>
</template>

<script>
    import { apiGet, apiPost, ApiError } from '../api/client'

    export default {
        name: 'TasksView',
        props: { id: { type: String, required: true } },
        data() {
            return {
                items: [],
                filterStatus: 'open',
                busy: false,
                err: '',
                genBusy: false,
                genErr: '',
                genRaw: '',

                jsonStr: '', // AI 输出的原始 JSON 字符串（调试用，非必须）
                tasks:'', // AI 解析后的任务数组（调试用，非必须）

                sentBodyStr: '', // 发送给 AI 的 JSON 字符串（调试用，非必须）;

                editingId: null,
                editForm: {
                  title: '',
                  priority: 'P1',
                  reason: '',
                  actionsText: '',
                  verify: '',
                },
            }
        },
        computed: {
            repoId() {
            return Number(this.id)
            },
        },
        mounted() {
            this.load()
        },
        methods: {
            startEdit(t) {
              this.editingId = t.id
              this.editForm = {
                title: t.title || '',
                priority: t.priority || 'P1',
                reason: t.reason || '',
                actionsText: (this.parsedActions(t) || []).join('\n'),
                verify: t.verify || '',
              }
            },
            cancelEdit() {
              this.editingId = null
            },
            async saveEdit(taskId) {
              this.err = ''
              this.busy = true
              try {
                const actionsArr = String(this.editForm.actionsText || '')
                  .split('\n')
                  .map(s => s.trim())
                  .filter(Boolean)

                const payload = {
                  title: this.editForm.title,
                  priority: this.editForm.priority,
                  reason: this.editForm.reason,
                  verify: this.editForm.verify,
                  actions_json: JSON.stringify(actionsArr),
                }

                await apiPatch(`/api/tasks/${taskId}`, payload)

                this.editingId = null
                await this.load()
              } catch (e) {
                this.err = this.formatErr(e)
              } finally {
                this.busy = false
              }
            },
            normalizeTasksForPost(tasks) {
                const out = []
                for (const t of (tasks || [])) {
                if (!t || typeof t !== 'object') continue
                const title = String(t.title ?? '').trim()
                if (!title) continue

                out.push({
                    title,
                    priority: (t.priority === 'P0' || t.priority === 'P1' || t.priority === 'P2') ? t.priority : 'P2',
                    reason: String(t.reason ?? ''),
                    actions: Array.isArray(t.actions) ? t.actions.map(x => String(x)) : [], // 关键：别用 undefined
                    expected_benefit: String(t.expected_benefit ?? ''),
                    verify: String(t.verify ?? ''),
                    source: String(t.source ?? 'ai'),
                    ai_conversation_id: t.ai_conversation_id ?? undefined,
                })
                }
                return out
            },
            formatErr(e) {
                if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
                if (e instanceof Error) return e.message
                return String(e)
            },
            async load() {
                this.err = ''
                this.busy = true
                try {
                    const qs = this.filterStatus ? `?status=${encodeURIComponent(this.filterStatus)}&limit=200` : '?limit=200'
                    const data = await apiGet(`/api/repos/${this.repoId}/tasks${qs}`)
                    this.items = data.items ?? data
                } catch (e) {
                    this.err = this.formatErr(e)
                } finally {
                    this.busy = false
                }
            },
            parsedActions(t) {
                const raw = t.actions_json || '[]'
                try {
                    const arr = JSON.parse(raw)
                    return Array.isArray(arr) ? arr : []
                } catch (_) {
                    return []
                }
            },
            pillClass(p) {
                if (p === 'P0') return 'p0'
                if (p === 'P1') return 'p1'
                return 'p2'
            },
            buildAiPrompt() {
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

                // 1) 优先提取 ```json fenced block
                const fenced = s.match(/```json\s*([\s\S]*?)\s*```/i)
                if (fenced && fenced[1]) return fenced[1].trim()

                // 2) 再尝试提取第一段平衡的 {...}
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

                // 3) 如果顶层是数组，也支持 [...]
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
            async askAi() {
              const res = await fetch('/api/ai/ask', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ repo_id: this.repoId, question: this.buildAiPrompt() }),
              })
              const text = await res.text()
              if (!res.ok) throw new ApiError(res.status, 'POST /api/ai/ask failed', text)
              const data = text ? JSON.parse(text) : {}
              if (data.success === false) throw new Error(data.error || 'AI failed')
              return data
            },
            parseAiTasks(answerText) {
              this.jsonStr = this.extractJsonObject(answerText)
              if (!this.jsonStr) throw new Error('AI 输出中未找到可解析的 JSON(请展开查看原始输出).')

              let payload = null
              try { payload = JSON.parse(this.jsonStr) } catch (_) { payload = null }

              const tasksArr =
                (payload && Array.isArray(payload.tasks) && payload.tasks) ||
                (Array.isArray(payload) && payload) ||
                null

              if (!tasksArr) throw new Error('AI JSON 缺少 tasks 数组（期望 { "tasks":[...] } 或顶层数组）。')
              return tasksArr
            },
            async postTasks(cleaned) {
              const postBody = { items: cleaned }
              this.sentBodyStr = JSON.stringify(postBody, null, 2)
              return await apiPost(`/api/repos/${this.repoId}/tasks`, postBody)
            },
            async generateFromAi() {
                this.genErr = ''
                this.genRaw = ''
                this.sentBodyStr = ''
                this.genBusy = true
                this.jsonStr = ''
                this.tasks = null

                try {
                  const data = await this.askAi()
                  this.genRaw = data.answer ?? ''

                  const tasksArr = this.parseAiTasks(this.genRaw)
                  const cleaned = this.normalizeTasksForPost(tasksArr)

                  this.tasks = cleaned
                  if (!cleaned.length) throw new Error('解析到的 tasks 为空（可能 title 全为空/格式不对）。')

                  await this.postTasks(cleaned)
                  await this.load()
                } catch (e) {
                  this.genErr = this.formatErr(e)
                } finally {
                  this.genBusy = false
                }
            },
            async markDone(taskId) {
            this.err = ''
            this.busy = true
            try {
                await apiPatch(`/api/tasks/${taskId}`, { status: 'done' })
                await this.load()
            } catch (e) {
                this.err = this.formatErr(e)
            } finally {
                this.busy = false
            }
            },
            async reopen(taskId) {
            this.err = ''
            this.busy = true
            try {
                await apiPatch(`/api/tasks/${taskId}`, { status: 'open' })
                await this.load()
            } catch (e) {
                this.err = this.formatErr(e)
            } finally {
                this.busy = false
            }
            },
            async remove(taskId) {
            this.err = ''
            this.busy = true
            try {
                await fetch(`/api/tasks/${taskId}`, { method: 'DELETE' })
                await this.load()
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

    /* 垂直间距（常用）：每条之间有 8px 间隔 */
    .actions-list { 
      padding: 0 0 0 20px; /* 内边距：左侧缩进，右侧紧贴边框 */
      margin: 0 5px 5px 5px;
    }
    .actions-list .action-item 
    { 
      padding: 0 0 0 0;
      margin: 0 0 5px 0; 
    }

    .row { display:flex; gap:10px; align-items:center; margin: 8px 0 12px; }
    .row-between { justify-content: space-between; }
    .tbl { border-collapse: collapse; width: 100%; }
    .tbl th, .tbl td { border: 1px solid #ddd; padding: 8px; vertical-align: top; }

    .ops { padding: 8px; } /* 确保和其他 td 一致 */

    .ops-inner {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
      align-items: flex-start;
      justify-content: center;
      min-height: 50px;           /* ✅ 让它至少接近 textarea 的最小高度 */
    }

    .btn { 
      height: 30px; 
      line-height: 28px; 
      padding: 0 10px; 
    }

    .btn-danger { border: 1px solid #ef4444; background: #fff; color: #b91c1c; }

    .ctl { height: 30px; }
    .ta { width: 100%; min-height: 72px; resize: vertical; box-sizing: border-box; }
    .title { min-width: 220px; }
    .wrap { max-width: 420px; white-space: pre-wrap; }
    .card { border:1px solid #e5e7eb; padding:12px; border-radius:8px; background:#fff; margin: 12px 0; }
    .err { color: #b00020; white-space: pre-wrap; }
    .muted { color: #6b7280; }
    .pill { display:inline-block; padding:2px 10px; border-radius:999px; font-weight:700; font-size:12px; }
    .p0 { background:#fee2e2; color:#991b1b; }
    .p1 { background:#fef3c7; color:#92400e; }
    .p2 { background:#e0e7ff; color:#3730a3; }
    .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; white-space: pre-wrap; }

    .pre-scroll {
        max-height: 360px;
        overflow: auto;
        white-space: pre; /* 保留格式，配合滚动条看完整内容 */
    }
</style>
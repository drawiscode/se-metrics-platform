<template>
  <section>
    <h2>AI 问答</h2>

    <div class="row">
      <input
        v-model="repoIdInput"
        inputmode="numeric"
        placeholder="repo_id(可选,留空=全局)"
      />
      <input v-model="question" class="q" placeholder="输入问题..." />

      <button :disabled="busy || !question.trim()" @click="ask">提问</button>
      <button :disabled="busy" @click="loadConvs">刷新历史</button>
    </div>

    <p class="muted">
      当前范围：
      <b>{{ scopeLabel }}</b>
      <span v-if="resolvedRepoId > 0">(repo_id={{ resolvedRepoId }})</span>
    </p>

    <p v-if="err" class="err">{{ err }}</p>

    <div v-if="answer" class="card">
      <h3>Answer</h3>
      <pre class="pre">{{ answer }}</pre>
    </div>

    <div class="card">
      <h3>Conversations</h3>
      <table class="tbl" v-if="convs.length">
        <thead><tr><th>time</th><th>question</th><th>op</th></tr></thead>
        <tbody>
          <tr v-for="c in convs" :key="c.id">
            <td>{{ c.created_at }}</td>
            <td>{{ c.question }}</td>
            <td><RouterLink :to="`/ai/conversations/${c.id}`">查看</RouterLink></td>
          </tr>
        </tbody>
      </table>
      <p v-else class="muted">暂无对话历史</p>
    </div>
  </section>
</template>

<script>
import { apiGet, ApiError } from '../api/client'

export default {
  name: 'AiView',
  data() {
    return {
      repoIdInput: '',     // 允许为空
      resolvedRepoId: 0,   // 实际使用的 repo_id(0=全局)
      question: '',
      answer: '',
      convs: [],
      err: '',
      busy: false,
      _repoTimer: null,
    }
  },

  computed: {
    scopeLabel() {
      return this.resolvedRepoId > 0 ? '单仓库' : '全局（所有仓库）'
    },
    parsedRepoId() {
      const s = String(this.repoIdInput || '').trim()
      if (!s) return null
      const n = Number(s)
      return Number.isFinite(n) ? n : null
    },
  },

  mounted() {
    // 支持 /ai?repo_id=7(可选)
    const q = this.$route?.query ?? {}
    if (q.repo_id != null && String(q.repo_id).trim() !== '') {
      this.repoIdInput = String(q.repo_id)
    }

    // 初次进入：不强制 repo_id,默认全局加载历史
    this.resolveScopeAndLoadConvsDebounced()
  },

  watch: {
    repoIdInput() {
      this.resolveScopeAndLoadConvsDebounced()
    },
  },

  beforeUnmount() {
    if (this._repoTimer) clearTimeout(this._repoTimer)
  },

  methods: {
    formatErr(e) {
      if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
      if (e instanceof Error) return e.message
      return String(e)
    },

    resolveScopeAndLoadConvsDebounced() {
      if (this._repoTimer) clearTimeout(this._repoTimer)
      this._repoTimer = setTimeout(async () => {
        await this.resolveRepoScope()
        await this.loadConvs()
      }, 300)
    },

    async repoExists(repoId) {
      try {
        await apiGet(`/api/repos/${repoId}`)
        return true
      } catch (e) {
        if (e instanceof ApiError && e.status === 404) return false
        // 其它错误（例如后端挂了）仍抛出
        throw e
      }
    },

    async resolveRepoScope() {
      this.err = ''

      const rid = this.parsedRepoId
      if (!rid || rid <= 0) {
        this.resolvedRepoId = 0
        // 同步 URL（可选）
        this.$router.replace({ path: '/ai' }).catch(() => {})
        return
      }

      const ok = await this.repoExists(rid)
      if (ok) {
        this.resolvedRepoId = rid
        this.$router.replace({ path: '/ai', query: { repo_id: String(rid) } }).catch(() => {})
      } else {
        // 仓库不存在：降级为全局
        this.resolvedRepoId = 0
        this.$router.replace({ path: '/ai' }).catch(() => {})
      }
    },

    async ask() {
      this.err = ''
      this.busy = true
      this.answer = ''

      try {
        // 提问前再确认一次 scope(避免用户刚改完 repoIdInput)
        await this.resolveRepoScope()

        const rid = this.resolvedRepoId
        if (this.parsedRepoId && this.parsedRepoId > 0 && rid === 0) {
          this.err = `提示:repo_id=${this.parsedRepoId} 不存在，已按全局知识库提问。`
        }

        const res = await fetch('/api/ai/ask', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ repo_id: rid, question: this.question.trim() }),
        })
        const text = await res.text()
        if (!res.ok) throw new ApiError(res.status, 'POST /api/ai/ask failed', text)

        const data = text ? JSON.parse(text) : {}
        if (data.success === false && data.error) {
          this.err = data.error
        }
        this.answer = data.answer ?? ''
        this.question = ''
        await this.loadConvs()
      } catch (e) {
        this.err = this.formatErr(e)
      } finally {
        this.busy = false
      }
    },

    async loadConvs() {
      this.err = this.err || ''
      this.busy = true
      try {
        const rid = this.resolvedRepoId
        const url = rid > 0
          ? `/api/ai/conversations?repo_id=${rid}&limit=20`
          : `/api/ai/conversations?limit=20`
        const data = await apiGet(url)
        this.convs = data.items ?? data
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
  .row { display:flex; gap:8px; align-items:center; margin: 8px 0 12px; }
  .q { flex: 1; }
  input { padding: 6px 8px; }
  .card { border:1px solid #e5e7eb; padding:12px; border-radius:8px; margin: 12px 0; background:#fff; }
  .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; white-space: pre-wrap; }
  .tbl { border-collapse: collapse; width: 100%; }
  .tbl th, .tbl td { border: 1px solid #ddd; padding: 6px 8px; }
  .err { color: #b00020; white-space: pre-wrap; }
  .muted { color: #6b7280; margin: 6px 0 0; }
</style>
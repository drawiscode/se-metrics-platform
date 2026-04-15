<template>
  <section>
    <h2>AI 问答</h2>

    <div class="row">
      <input v-model.number="repoId" type="number" min="1" placeholder="repo_id" />
      <input v-model="question" class="q" placeholder="输入问题..." />
      <button :disabled="busy || !question.trim() || repoId<=0" @click="ask">提问</button>
      <!-- 可保留按钮当手动触发；也可删掉 -->
      <button :disabled="busy || repoId<=0" @click="loadConvs">刷新历史</button>
    </div>

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
      repoId: 1,
      question: '',
      answer: '',
      convs: [],
      err: '',
      busy: false,
      _repoTimer: null,
    }
  },

  mounted() {
    // 支持 /ai?repo_id=7
    const q = this.$route && this.$route.query ? this.$route.query : {}
    const rid = Number(q.repo_id)
    if (Number.isFinite(rid) && rid > 0) this.repoId = rid

    // 首次进入自动加载一次
    if (this.repoId > 0) this.loadConvs()
  },

  watch: {
    repoId(newVal, oldVal) {
      if (newVal === oldVal) return
      if (this._repoTimer) clearTimeout(this._repoTimer)

      this._repoTimer = setTimeout(() => {
        if (Number.isFinite(newVal) && newVal > 0) {
          // 更新 URL（可选）：让返回/刷新时保留 repo_id
          if (this.$route?.query?.repo_id !== String(newVal)) {
            this.$router.replace({ path: '/ai', query: { repo_id: String(newVal) } })
              .catch(() => {}) // 避免重复导航报错
          }
          this.loadConvs()
        } else {
          this.convs = []
        }
      }, 100)
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

    async ask() {
      this.err = ''
      this.busy = true
      this.answer = ''

      try {
        const res = await fetch('/api/ai/ask', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ repo_id: this.repoId, question: this.question.trim() }),
        })
        const text = await res.text()
        if (!res.ok) throw new ApiError(res.status, 'POST /api/ai/ask failed', text)

        const data = text ? JSON.parse(text) : {}
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
      this.err = ''
      this.busy = true
      try {
        const data = await apiGet(`/api/ai/conversations?repo_id=${this.repoId}&limit=20`)
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
    .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; }
    .tbl { border-collapse: collapse; width: 100%; }
    .tbl th, .tbl td { border: 1px solid #ddd; padding: 6px 8px; }
    .err { color: #b00020; white-space: pre-wrap; }
</style>
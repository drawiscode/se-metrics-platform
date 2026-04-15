<template>
  <section>
    <h2>AI 问答</h2>

    <div class="row">
      <input v-model.number="repoId" type="number" min="1" placeholder="repo_id" />
      <input v-model="question" class="q" placeholder="输入问题..." />
      <button :disabled="busy || !question.trim() || repoId<=0" @click="ask">提问</button>
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
        <thead><tr><th>time</th><th>question</th></tr></thead>
        <tbody>
          <tr v-for="c in convs" :key="c.id">
            <td>{{ c.created_at }}</td>
            <td>{{ c.question }}</td>
          </tr>
        </tbody>
      </table>
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
            }
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
                    // 兼容：后端可能返回 {items:[...]} 或直接返回数组
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
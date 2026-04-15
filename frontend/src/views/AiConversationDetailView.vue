<template>
  <section>
    <div class="row">
      <h2>对话详情 #{{ convId }}</h2>
      <RouterLink :to="backLink">返回 AI</RouterLink>
    </div>

    <p v-if="err" class="err">{{ err }}</p>
    <p v-if="busy">加载中...</p>

    <div v-if="conv" class="card">
      <div class="meta">
        <div><b>repo_id:</b> {{ conv.repo_id }}</div>
        <div><b>model:</b> {{ conv.model }}</div>
        <div><b>created_at:</b> {{ conv.created_at }}</div>
      </div>

      <h3>Question</h3>
      <pre class="pre">{{ conv.question }}</pre>

      <h3>Answer</h3>
      <pre class="pre">{{ conv.answer }}</pre>

      <h3>Evidence</h3>
      <table class="tbl" v-if="evidence.length">
        <thead>
          <tr><th>source</th><th>title</th><th>snippet</th></tr>
        </thead>
        <tbody>
          <tr v-for="(e, idx) in evidence" :key="idx">
            <td>{{ e.source_type }} #{{ e.source_id }}</td>
            <td>{{ e.title }}</td>
            <td><pre class="snippet">{{ e.content || e.snippet }}</pre></td>
          </tr>
        </tbody>
      </table>

      <p v-else class="muted">无 evidence 记录</p>
    </div>

  </section>
</template>

<script>
import { apiGet, ApiError } from '../api/client'

export default {
  name: 'AiConversationDetailView',
  props: {
    id: { type: String, required: true },
  },
  data() {
    return {
      conv: null,
      evidence: [],
      err: '',
      busy: false,
    }
  },
  computed: {
    convId() {
      return Number(this.id)
    },
    backLink() {
      const rid = this.conv && this.conv.repo_id ? this.conv.repo_id : ''
      return rid ? `/ai?repo_id=${rid}` : '/ai'
    },
  },
  mounted() {
    this.load()
  },
  methods: {
    formatErr(e) {
      if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
      if (e instanceof Error) return e.message
      return String(e)
    },

    async load() {
      this.err = ''
      this.busy = true
      try {
        const data = await apiGet(`/api/ai/conversations/${this.convId}`)
        this.conv = data
        // 后端返回 evidence_json 是数组(JSON),这里做兼容
        this.evidence = Array.isArray(data.evidence_json) ? data.evidence_json : []
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
    .row { display:flex; gap:12px; align-items:center; margin-bottom: 12px; }
    .card { border:1px solid #e5e7eb; padding:12px; border-radius:8px; background:#fff; }
    .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; white-space: pre-wrap; }
    .tbl { border-collapse: collapse; width: 100%; }
    .tbl th, .tbl td { border: 1px solid #ddd; padding: 6px 8px; vertical-align: top; }
    .snippet { margin:0; white-space: pre-wrap; max-height: 220px; overflow:auto; }
    .err { color: #b00020; white-space: pre-wrap; }
    .muted { color: #6b7280; }
    .meta { display:flex; gap:16px; flex-wrap: wrap; margin-bottom: 8px; }
</style>
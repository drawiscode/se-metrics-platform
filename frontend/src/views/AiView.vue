<template>
  <div class="ai-chat-page">
    <aside class="chat-sidebar">
      <div class="sidebar-header">
        <h3>Conversations</h3>
        <button class="btn-new" :disabled="busy" @click="newThread" title="New conversation">+</button>
      </div>
      <div class="repo-select">
        <input v-model="repoIdInput" inputmode="numeric" placeholder="repo_id (empty=global)" class="repo-input" @change="onRepoChange" />
      </div>
      <div class="thread-list">
        <div v-for="t in threads" :key="t.thread_id" class="thread-item" :class="{ active: t.thread_id === activeThreadId }" @click="selectThread(t.thread_id)">
          <div class="thread-title">{{ truncateTitle(t.title) }}</div>
          <div class="thread-meta">
            <span>{{ t.msg_count }} msgs</span>
            <button class="btn-del" @click.stop="deleteThread(t.thread_id)" title="Delete">&times;</button>
          </div>
        </div>
        <p v-if="!threads.length && !busy" class="muted">No conversations yet</p>
      </div>
    </aside>
    <main class="chat-main">
      <div v-if="!activeThreadId && !messages.length" class="chat-welcome">
        <h2>DevInsight AI</h2>
        <p>Select a conversation or start a new one. Ask about repositories, code quality, contributors, and more.</p>
      </div>
      <div class="chat-header" v-if="activeThreadId">
        <span>Thread #{{ activeThreadId }}</span>
        <span class="scope-label">{{ scopeLabel }}</span>
      </div>
      <div class="chat-messages" ref="msgContainer">
        <div v-if="err" class="chat-err">{{ err }}</div>
        <div v-for="(msg, idx) in messages" :key="idx" class="msg-group">
          <div class="msg msg-user">
            <div class="msg-avatar">Q</div>
            <div class="msg-bubble user-bubble"><pre class="msg-text">{{ msg.question }}</pre></div>
          </div>
          <div class="msg msg-assistant">
            <div class="msg-avatar">AI</div>
            <div class="msg-bubble ai-bubble">
              <pre class="msg-text">{{ msg.answer }}</pre>
              <div class="msg-meta" v-if="msg.model">{{ msg.model }} &middot; {{ msg.created_at }}</div>
            </div>
          </div>
        </div>
        <div v-if="busy" class="msg msg-assistant">
          <div class="msg-avatar">AI</div>
          <div class="msg-bubble ai-bubble typing">Thinking...</div>
        </div>
      </div>
      <div class="chat-input-area">
        <textarea v-model="question" class="chat-input" placeholder="Type your question... (Enter to send, Shift+Enter for newline)" rows="2" :disabled="busy" @keydown.enter.exact.prevent="ask"></textarea>
        <button class="btn-send" :disabled="busy || !question.trim()" @click="ask">Send</button>
      </div>
    </main>
  </div>
</template>

<script>
import { apiGet, ApiError } from '../api/client'

export default {
  name: 'AiView',
  data() {
    return {
      repoIdInput: '',
      resolvedRepoId: 0,
      activeThreadId: 0,
      question: '',
      messages: [],
      threads: [],
      err: '',
      busy: false,
      _repoTimer: null,
    }
  },
  computed: {
    scopeLabel() {
      return this.resolvedRepoId > 0 ? `repo #${this.resolvedRepoId}` : 'global'
    },
    parsedRepoId() {
      const s = String(this.repoIdInput || '').trim()
      if (!s) return null
      const n = Number(s)
      return Number.isFinite(n) && n > 0 ? n : null
    },
  },
  mounted() {
    const q = this.$route?.query ?? {}
    if (q.repo_id != null && String(q.repo_id).trim() !== '') {
      this.repoIdInput = String(q.repo_id)
    }
    this.resolveScopeAndLoad()
  },
  watch: {
    repoIdInput() {
      if (this._repoTimer) clearTimeout(this._repoTimer)
      this._repoTimer = setTimeout(() => { this.resolveScopeAndLoad() }, 300)
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
    truncateTitle(t) {
      if (!t) return 'New conversation'
      return t.length > 40 ? t.slice(0, 40) + '...' : t
    },
    scrollToBottom() {
      this.$nextTick(() => {
        const el = this.$refs.msgContainer
        if (el) el.scrollTop = el.scrollHeight
      })
    },
    async repoExists(repoId) {
      try { await apiGet(`/api/repos/${repoId}`); return true }
      catch (e) { if (e instanceof ApiError && e.status === 404) return false; throw e }
    },
    async resolveScopeAndLoad() {
      this.err = ''
      const rid = this.parsedRepoId
      if (!rid) {
        this.resolvedRepoId = 0
        this.$router.replace({ path: '/ai' }).catch(() => {})
      } else {
        const ok = await this.repoExists(rid)
        this.resolvedRepoId = ok ? rid : 0
        this.$router.replace({ path: '/ai', query: rid && ok ? { repo_id: String(rid) } : {} }).catch(() => {})
      }
      await this.loadThreads()
    },
    onRepoChange() {
      this.activeThreadId = 0
      this.messages = []
    },
    async loadThreads() {
      this.busy = true
      try {
        const rid = this.resolvedRepoId
        const url = rid > 0 ? `/api/ai/threads?repo_id=${rid}` : '/api/ai/threads'
        const data = await apiGet(url)
        this.threads = data.items ?? []
      } catch (e) { this.err = this.formatErr(e) }
      finally { this.busy = false }
    },
    async newThread() {
      try {
        const res = await fetch('/api/ai/threads', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ repo_id: this.resolvedRepoId }),
        })
        const data = await res.json()
        if (data.thread_id) {
          await this.loadThreads()
          this.activeThreadId = data.thread_id
          this.messages = []
          this.err = ''
        }
      } catch (e) { this.err = this.formatErr(e) }
    },
    async selectThread(tid) {
      this.activeThreadId = tid
      this.err = ''
      this.busy = true
      try {
        const data = await apiGet(`/api/ai/threads/${tid}/messages`)
        this.messages = data.messages ?? []
        this.scrollToBottom()
      } catch (e) { this.err = this.formatErr(e) }
      finally { this.busy = false }
    },
    async deleteThread(tid) {
      if (!confirm('Delete this conversation?')) return
      try {
        await fetch(`/api/ai/threads/${tid}`, { method: 'DELETE' })
        if (this.activeThreadId === tid) { this.activeThreadId = 0; this.messages = [] }
        await this.loadThreads()
      } catch (e) { this.err = this.formatErr(e) }
    },
    async ask() {
      const q = this.question.trim()
      if (!q || this.busy) return
      this.err = ''
      if (!this.activeThreadId) {
        try {
          const res = await fetch('/api/ai/threads', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ repo_id: this.resolvedRepoId }),
          })
          const data = await res.json()
          if (data.thread_id) { this.activeThreadId = data.thread_id; await this.loadThreads() }
        } catch (e) { this.err = this.formatErr(e); return }
      }
      this.messages.push({ question: q, answer: '', model: '', created_at: '' })
      this.question = ''
      this.busy = true
      this.scrollToBottom()
      try {
        const res = await fetch('/api/ai/ask', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ repo_id: this.resolvedRepoId, question: q, thread_id: this.activeThreadId }),
        })
        const text = await res.text()
        if (!res.ok) throw new ApiError(res.status, 'POST /api/ai/ask failed', text)
        const data = text ? JSON.parse(text) : {}
        if (data.success === false && data.error) { this.err = data.error; this.messages.pop() }
        else {
          const last = this.messages[this.messages.length - 1]
          if (last) { last.answer = data.answer ?? ''; last.model = data.model ?? ''; last.created_at = new Date().toLocaleString() }
        }
        await this.loadThreads()
      } catch (e) { this.err = this.formatErr(e); this.messages.pop() }
      finally { this.busy = false; this.scrollToBottom() }
    },
  },
}
</script>

<style scoped>
.ai-chat-page { display:flex; height:calc(100vh - 140px); min-height:500px; border:1px solid #e5e7eb; border-radius:10px; overflow:hidden; background:#fff; }
.chat-sidebar { width:260px; min-width:220px; border-right:1px solid #e5e7eb; display:flex; flex-direction:column; background:#f9fafb; }
.sidebar-header { display:flex; align-items:center; justify-content:space-between; padding:12px 14px; border-bottom:1px solid #e5e7eb; }
.sidebar-header h3 { margin:0; font-size:14px; font-weight:600; }
.btn-new { width:28px; height:28px; border:1px solid #d1d5db; border-radius:6px; background:#fff; font-size:18px; cursor:pointer; display:flex; align-items:center; justify-content:center; }
.btn-new:hover { background:#e5e7eb; }
.repo-select { padding:8px 12px; }
.repo-input { width:100%; box-sizing:border-box; padding:6px 8px; font-size:12px; border:1px solid #d1d5db; border-radius:6px; }
.thread-list { flex:1; overflow-y:auto; padding:4px 0; }
.thread-item { padding:10px 14px; cursor:pointer; border-bottom:1px solid #f0f0f0; transition:background .15s; }
.thread-item:hover { background:#eef2ff; }
.thread-item.active { background:#e0e7ff; }
.thread-title { font-size:13px; font-weight:500; color:#1f2937; line-height:1.4; }
.thread-meta { display:flex; align-items:center; justify-content:space-between; font-size:11px; color:#9ca3af; margin-top:3px; }
.btn-del { background:none; border:none; color:#ef4444; cursor:pointer; font-size:16px; padding:0 4px; line-height:1; }
.btn-del:hover { color:#dc2626; }
.chat-main { flex:1; display:flex; flex-direction:column; min-width:0; }
.chat-welcome { flex:1; display:flex; flex-direction:column; align-items:center; justify-content:center; color:#9ca3af; gap:8px; }
.chat-welcome h2 { font-size:24px; color:#6b7280; margin:0; }
.chat-header { padding:10px 16px; border-bottom:1px solid #e5e7eb; font-size:13px; font-weight:600; display:flex; gap:12px; align-items:center; }
.scope-label { font-weight:400; color:#6b7280; font-size:12px; }
.chat-messages { flex:1; overflow-y:auto; padding:16px; display:flex; flex-direction:column; gap:16px; }
.chat-err { background:#fef2f2; color:#b91c1c; padding:8px 12px; border-radius:8px; font-size:13px; }
.msg-group { display:flex; flex-direction:column; gap:8px; }
.msg { display:flex; gap:10px; max-width:85%; }
.msg-user { align-self:flex-end; flex-direction:row-reverse; }
.msg-assistant { align-self:flex-start; }
.msg-avatar { width:32px; height:32px; border-radius:50%; display:flex; align-items:center; justify-content:center; font-size:13px; font-weight:700; color:#fff; flex-shrink:0; }
.msg-user .msg-avatar { background:#6366f1; }
.msg-assistant .msg-avatar { background:#10b981; }
.msg-bubble { padding:10px 14px; border-radius:12px; font-size:14px; line-height:1.55; }
.user-bubble { background:#eef2ff; color:#1f2937; }
.ai-bubble { background:#f3f4f6; color:#1f2937; }
.ai-bubble.typing { color:#9ca3af; font-style:italic; }
.msg-text { margin:0; white-space:pre-wrap; font-family:inherit; word-break:break-word; }
.msg-meta { font-size:11px; color:#9ca3af; margin-top:4px; }
.chat-input-area { padding:12px 16px; border-top:1px solid #e5e7eb; display:flex; gap:10px; align-items:flex-end; }
.chat-input { flex:1; padding:10px 12px; border:1px solid #d1d5db; border-radius:10px; font-size:14px; font-family:inherit; resize:none; outline:none; }
.chat-input:focus { border-color:#6366f1; box-shadow:0 0 0 2px rgba(99,102,241,.15); }
.btn-send { padding:10px 18px; background:#6366f1; color:#fff; border:none; border-radius:10px; font-size:14px; font-weight:600; cursor:pointer; white-space:nowrap; }
.btn-send:hover:not(:disabled) { background:#4f46e5; }
.btn-send:disabled { opacity:.5; cursor:default; }
.muted { color:#9ca3af; font-size:13px; padding:12px; text-align:center; }
</style>
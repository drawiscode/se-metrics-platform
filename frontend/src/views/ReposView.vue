<template>
  <section>
    <h2>仓库列表</h2>

    <form class="row" @submit.prevent="createRepo">
      <input v-model="fullName" placeholder="owner/repo" />
      <button :disabled="busy || !fullName.trim()">添加</button>
      <button type="button" :disabled="busy" @click="load">刷新</button>
    </form>

    <p v-if="err" class="err">{{ err }}</p>

    <table class="tbl" v-if="items.length">
      <thead>
        <tr>
          <th>id</th>
          <th>full_name</th>
          <th>enabled</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="r in items" :key="r.id">
          <td>{{ r.id }}</td>
          <td><RouterLink :to="`/repos/${r.id}`">{{ r.full_name }}</RouterLink></td>
          <td>{{ r.enabled }}</td>
          <td class="ops">
            <button :disabled="busy" @click="sync(r.id)">sync(增量,当前还未添加参数,后续添加相应功能)</button>
            <button :disabled="busy" @click="syncFull(r.id)">sync(full)</button>
            <button :disabled="busy" @click="sync_commit_files(r.id)">sync_commit_files(先默认一次30条)</button>
          </td>
        </tr>
      </tbody>
    </table>

    <p v-if="syncSummary" class="ok">{{ syncSummary }}</p>
    <pre v-if="lastSync" class="pre">{{ lastSync }}</pre>
  </section>
</template>

<script>
  import { apiGet, apiPost, ApiError } from '../api/client'

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
      }
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

      setSyncResult(data) {
        this.lastSync = JSON.stringify(data, null, 2)

        if (data && data.ok && typeof data.kb_total_indexed !== 'undefined') {
          this.syncSummary =
            `同步成功：本次构建知识块 ${data.kb_total_indexed} 条 ` +
            `(Issue ${data.kb_issues_indexed ?? 0} / PR ${data.kb_pulls_indexed ?? 0} / ` +
            `Commit ${data.kb_commits_indexed ?? 0} / Release ${data.kb_releases_indexed ?? 0})`
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

      async sync(id) {
        this.err = ''
        this.busy = true
        this.syncSummary = ''
        this.lastSync = ''
        try {
          const data = await apiPost(`/api/repos/${id}/sync`)
          this.setSyncResult(data)
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
          const data = await apiPost(`/api/repos/${id}/sync?mode=full&issues_pages_count=1&pulls_pages_count=1&commits_pages_count=1&releases_pages_count=1`)
          this.setSyncResult(data)
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
      }

    },
  }
</script>

<style scoped>
    .row { display:flex; gap:8px; align-items:center; margin: 8px 0 12px; }
    input { width: 320px; padding: 6px 8px; }
    .tbl { border-collapse: collapse; width: 100%; }
    .tbl th, .tbl td { border: 1px solid #ddd; padding: 8px; }
    .ops { display:flex; gap:8px; }
    .err { color: #b00020; white-space: pre-wrap; }
    .ok { color: #0f7b3c; white-space: pre-wrap; margin: 10px 0; }
    .pre { background:#f6f8fa; padding:12px; border:1px solid #e5e7eb; overflow:auto; }
</style>
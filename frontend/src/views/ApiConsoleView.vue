<template>
  <section class="console">
    <div class="request-bar">
      <select v-model="method" class="method-select">
        <option v-for="m in methods" :key="m" :value="m">{{ m }}</option>
      </select>

      <div class="url-wrap">
        <input v-model="url" class="url-input" placeholder="http://localhost:3000/api/" />
        <button class="icon-btn" type="button" @click="clearUrl">×</button>
      </div>

      <button class="send-btn" type="button" :disabled="busy" @click="sendRequest">
        {{ busy ? '发送中...' : '发送' }}
      </button>
    </div>

    <div class="tab-bar">
      <button
        v-for="tab in tabs"
        :key="tab"
        class="tab-btn"
        :class="{ active: activeTab === tab }"
        type="button"
        @click="activeTab = tab"
      >
        {{ tab }}
      </button>
    </div>

    <div class="panel">
      <div v-if="activeTab === 'Params'" class="panel-inner">
        <div class="panel-head">
          <span>查询参数</span>
          <div class="panel-actions">
            <button type="button" class="ghost-btn" @click="encodeParams">编码</button>
            <button type="button" class="ghost-btn" @click="decodeParams">解码</button>
            <button type="button" class="ghost-btn" @click="addParam">新增</button>
          </div>
        </div>
        <table class="grid">
          <thead>
            <tr>
              <th>Key</th>
              <th>Value</th>
              <th>描述</th>
              <th>启用</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(row, idx) in params" :key="`param-${idx}`">
              <td><input v-model="row.key" class="cell-input" /></td>
              <td><input v-model="row.value" class="cell-input" /></td>
              <td><input v-model="row.desc" class="cell-input" /></td>
              <td><input type="checkbox" v-model="row.enabled" /></td>
              <td><button type="button" class="link-btn" @click="removeParam(idx)">删除</button></td>
            </tr>
          </tbody>
        </table>
      </div>

      <div v-if="activeTab === 'Authorization'" class="panel-inner">
        <div class="callout note">✅ 说明：当前后端未启用鉴权，默认不需要 Authorization。</div>
      </div>

      <div v-if="activeTab === 'Headers'" class="panel-inner">
        <div class="panel-head">
          <span>请求头</span>
          <div class="panel-actions">
            <button type="button" class="ghost-btn" @click="addHeaderTemplate">添加 JSON 头</button>
            <button type="button" class="ghost-btn" @click="addHeader">新增</button>
          </div>
        </div>
        <table class="grid">
          <thead>
            <tr>
              <th>Key</th>
              <th>Value</th>
              <th>描述</th>
              <th>启用</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(row, idx) in headers" :key="`header-${idx}`">
              <td><input v-model="row.key" class="cell-input" /></td>
              <td><input v-model="row.value" class="cell-input" /></td>
              <td><input v-model="row.desc" class="cell-input" /></td>
              <td><input type="checkbox" v-model="row.enabled" /></td>
              <td><button type="button" class="link-btn" @click="removeHeader(idx)">删除</button></td>
            </tr>
          </tbody>
        </table>
      </div>

      <div v-if="activeTab === 'Body'" class="panel-inner">
        <div class="body-types">
          <label v-for="type in bodyTypes" :key="type" class="radio">
            <input v-model="bodyType" type="radio" :value="type" :disabled="isGet" />
            <span>{{ type }}</span>
          </label>
        </div>

        <div v-if="isGet" class="callout warn">⚠️ 注意：GET 请求不支持请求体。</div>

        <div v-if="bodyType === 'raw'" class="raw-panel">
          <div class="panel-head">
            <div class="panel-actions">
              <select v-model="rawFormat" class="mini-select">
                <option value="json">JSON</option>
                <option value="text">Text</option>
                <option value="xml">XML</option>
              </select>
              <button type="button" class="ghost-btn" @click="beautifyJson" :disabled="rawFormat !== 'json'">格式化</button>
            </div>
          </div>
          <textarea v-model="rawBody" class="raw-input" placeholder="{\n  &quot;key&quot;: &quot;value&quot;\n}"></textarea>
        </div>

        <div v-if="bodyType !== 'raw' && bodyType !== 'none'" class="callout note">
          ✅ 说明：当前版本仅提供 raw JSON 请求体，其它类型可在后续扩展。
        </div>
      </div>

      <div v-if="activeTab === 'Response'" class="panel-inner">
        <div class="callout note">✅ 说明：响应结果已在下方展示，可切换视图与复制。</div>
      </div>
    </div>

    <div class="response">
      <div class="response-head">
        <div class="response-meta">
          <span class="status" :class="statusClass">{{ responseStatus || '-' }}</span>
          <span class="meta">{{ responseMs }} ms</span>
          <span class="meta">{{ responseSizeLabel }}</span>
        </div>
        <div class="response-actions">
          <button class="ghost-btn" type="button" @click="copyResponse">复制响应</button>
          <button class="ghost-btn" type="button" @click="downloadResponse">下载响应</button>
        </div>
      </div>

      <div class="view-tabs">
        <button
          v-for="view in responseViews"
          :key="view"
          class="view-btn"
          :class="{ active: responseView === view }"
          type="button"
          @click="responseView = view"
        >
          {{ view }}
        </button>
      </div>

      <div class="response-body">
        <pre v-if="responseView !== 'Preview'" class="code">{{ responseDisplay }}</pre>
        <iframe
          v-else
          class="preview"
          :srcdoc="responseView === 'Preview' ? responseText : ''"
          title="preview"
        ></iframe>
      </div>

      <p v-if="errorText" class="err">{{ errorText }}</p>
    </div>
  </section>
</template>

<script setup>
  import { computed, onMounted, ref, watch } from 'vue'
  import { apiRequest, ApiError } from '../api/client'

  const methods = ['GET', 'POST', 'PUT', 'DELETE', 'PATCH', 'OPTIONS', 'HEAD']
  const tabs = ['Params', 'Authorization', 'Headers', 'Body', 'Response']
  const responseViews = ['Pretty', 'Raw', 'Preview']
  const bodyTypes = ['none', 'raw', 'form-data', 'x-www-form-urlencoded', 'binary', 'GraphQL']

  const method = ref('GET')
  const url = ref('http://localhost:3000/api/')
  const activeTab = ref('Params')
  const busy = ref(false)
  const errorText = ref('')

  const params = ref([
    { key: '', value: '', desc: '', enabled: true },
  ])
  const headers = ref([
    { key: 'Content-Type', value: 'application/json', desc: '默认 JSON', enabled: true },
  ])

  const bodyType = ref('none')
  const rawFormat = ref('json')
  const rawBody = ref('')

  const responseStatus = ref('')
  const responseMs = ref(0)
  const responseText = ref('')
  const responseContentType = ref('')
  const responseView = ref('Pretty')

  // ===================== 状态持久化（自动缓存，切回不丢失） =====================
  const STORAGE_KEY = 'api_console_state'

  const saveState = () => {
    const state = {
      method: method.value,
      url: url.value,
      activeTab: activeTab.value,
      params: params.value,
      headers: headers.value,
      bodyType: bodyType.value,
      rawFormat: rawFormat.value,
      rawBody: rawBody.value,
    }
    try { sessionStorage.setItem(STORAGE_KEY, JSON.stringify(state)) } catch { /* ignore */ }
  }

  const restoreState = () => {
    try {
      const raw = sessionStorage.getItem(STORAGE_KEY)
      if (!raw) return
      const state = JSON.parse(raw)
      if (state.method) method.value = state.method
      if (state.url) url.value = state.url
      if (state.activeTab) activeTab.value = state.activeTab
      if (Array.isArray(state.params) && state.params.length) params.value = state.params
      if (Array.isArray(state.headers) && state.headers.length) headers.value = state.headers
      if (state.bodyType) bodyType.value = state.bodyType
      if (state.rawFormat) rawFormat.value = state.rawFormat
      if (state.rawBody || state.rawBody === '') rawBody.value = state.rawBody
    } catch { /* ignore */ }
  }

  onMounted(restoreState)

  // 监听关键字段，变化时自动保存
  watch([method, url, activeTab, params, headers, bodyType, rawFormat, rawBody], saveState, { deep: true })

  // ===================== 计算属性 =====================
  const isGet = computed(() => method.value === 'GET')

  const statusClass = computed(() => {
    const status = Number(responseStatus.value)
    if (!status) return ''
    if (status >= 200 && status < 300) return 'ok'
    if (status >= 400 && status < 500) return 'warn'
    if (status >= 500) return 'err'
    return ''
  })

  const responseSizeLabel = computed(() => {
    if (!responseText.value) return '0 B'
    const bytes = new Blob([responseText.value]).size
    if (bytes < 1024) return `${bytes} B`
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
    return `${(bytes / 1024 / 1024).toFixed(1)} MB`
  })

  const responseDisplay = computed(() => {
    if (responseView.value === 'Raw') return responseText.value || '-'
    if (!responseText.value) return '-'
    if (responseContentType.value.includes('application/json')) {
      try {
        return JSON.stringify(JSON.parse(responseText.value), null, 2)
      } catch {
        return responseText.value
      }
    }
    return responseText.value
  })

  const buildUrlWithParams = () => {
    const rawUrl = String(url.value || '').trim()
    if (!rawUrl) return ''
    const enabled = params.value.filter(row => row.enabled && row.key)
    if (!enabled.length) return rawUrl

    try {
      const base = rawUrl.startsWith('http') ? rawUrl : `http://local${rawUrl}`
      const u = new URL(base)
      enabled.forEach(row => {
        u.searchParams.set(row.key, row.value ?? '')
      })
      return rawUrl.startsWith('http') ? u.toString() : `${u.pathname}${u.search}`
    } catch {
      return rawUrl
    }
  }

  const toHeadersObject = () => {
    const out = {}
    headers.value.forEach(row => {
      if (!row.enabled || !row.key) return
      out[row.key] = row.value ?? ''
    })
    return out
  }

  const normalizeBody = () => {
    if (isGet.value || bodyType.value === 'none') return undefined
    if (bodyType.value !== 'raw') return undefined

    if (rawFormat.value === 'json') {
      const trimmed = String(rawBody.value || '').trim()
      if (!trimmed) return undefined
      return JSON.parse(trimmed)
    }

    return String(rawBody.value || '')
  }

  const applyRawContentType = (headersObj) => {
    if (bodyType.value !== 'raw') return headersObj
    if (rawFormat.value === 'json') {
      return { ...headersObj, 'Content-Type': 'application/json' }
    }
    if (rawFormat.value === 'xml') {
      return { ...headersObj, 'Content-Type': 'application/xml' }
    }
    return { ...headersObj, 'Content-Type': 'text/plain' }
  }

  const clearUrl = () => {
    url.value = ''
  }

  const addParam = () => {
    params.value.push({ key: '', value: '', desc: '', enabled: true })
  }

  const removeParam = (idx) => {
    params.value.splice(idx, 1)
  }

  const encodeParams = () => {
    params.value = params.value.map(row => ({
      ...row,
      value: row.value ? encodeURIComponent(row.value) : row.value,
    }))
  }

  const decodeParams = () => {
    params.value = params.value.map(row => {
      try {
        return { ...row, value: row.value ? decodeURIComponent(row.value) : row.value }
      } catch {
        return row
      }
    })
  }

  const addHeader = () => {
    headers.value.push({ key: '', value: '', desc: '', enabled: true })
  }

  const addHeaderTemplate = () => {
    headers.value.push({ key: 'Content-Type', value: 'application/json', desc: 'JSON 模板', enabled: true })
  }

  const removeHeader = (idx) => {
    headers.value.splice(idx, 1)
  }

  const beautifyJson = () => {
    if (rawFormat.value !== 'json') return
    try {
      rawBody.value = JSON.stringify(JSON.parse(rawBody.value), null, 2)
    } catch {
      errorText.value = 'JSON 请求体格式不正确。'
    }
  }

  const copyResponse = async () => {
    if (!responseText.value) return
    try {
      await navigator.clipboard.writeText(responseText.value)
    } catch {
      // ignore
    }
  }

  const downloadResponse = () => {
    const blob = new Blob([responseText.value || ''], { type: 'text/plain' })
    const urlObj = URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = urlObj
    link.download = 'response.txt'
    link.click()
    URL.revokeObjectURL(urlObj)
  }

  const formatErr = (e) => {
    if (e instanceof ApiError) return `${e.status} ${e.message}\n${e.bodyText ?? ''}`
    if (e instanceof Error) return e.message
    return String(e)
  }

  const sendRequest = async () => {
    errorText.value = ''
    responseStatus.value = ''
    responseText.value = ''
    responseContentType.value = ''
    responseMs.value = 0

    const targetUrl = buildUrlWithParams()
    if (!targetUrl) {
      errorText.value = '请求 URL 不能为空。'
      return
    }

    let bodyPayload
    try {
      bodyPayload = normalizeBody()
    } catch {
      errorText.value = 'JSON 请求体格式不正确。'
      return
    }

    const start = performance.now()
    busy.value = true
    try {
      const headersObj = applyRawContentType(toHeadersObject())
      const result = await apiRequest(method.value, targetUrl, bodyPayload, { headers: headersObj })
      responseStatus.value = result.status
      responseText.value = result.text || ''
      responseContentType.value = headersObj['Accept'] || 'application/json'
    } catch (e) {
      errorText.value = formatErr(e)
      if (e instanceof ApiError) {
        responseStatus.value = e.status
        responseText.value = e.bodyText || ''
      }
    } finally {
      responseMs.value = Math.round(performance.now() - start)
      busy.value = false
    }
  }
</script>

<style scoped>
  .console {
    display: flex;
    flex-direction: column;
    gap: 12px;
    color: #1e293b;
  }

  .request-bar {
    display: grid;
    grid-template-columns: 120px 1fr 120px;
    gap: 10px;
    align-items: center;
    background: #ffffff;
    border: 1px solid #e5e7eb;
    border-radius: 10px;
    padding: 10px;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.06);
  }

  .method-select,
  .url-input,
  .mini-select {
    background: #f9fafb;
    color: #1e293b;
    border: 1px solid #d1d5db;
    border-radius: 8px;
    height: 36px;
    padding: 0 10px;
    font-size: 13px;
    outline: none;
  }

  .method-select:focus,
  .url-input:focus,
  .mini-select:focus {
    border-color: #3b82f6;
    box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.15);
  }

  .url-wrap {
    display: flex;
    align-items: center;
    gap: 6px;
    background: #f9fafb;
    border: 1px solid #d1d5db;
    border-radius: 8px;
    padding: 0 6px 0 0;
  }

  .url-wrap:focus-within {
    border-color: #3b82f6;
    box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.15);
  }

  .url-input {
    width: 100%;
    border: none;
    background: transparent;
  }

  .url-input:focus {
    border-color: transparent;
    box-shadow: none;
  }

  .icon-btn {
    background: transparent;
    border: none;
    color: #9ca3af;
    cursor: pointer;
    font-size: 18px;
    padding: 0 6px;
    line-height: 1;
  }

  .icon-btn:hover {
    color: #6b7280;
  }

  .send-btn {
    background: #2563eb;
    color: #ffffff;
    border: none;
    border-radius: 8px;
    height: 36px;
    font-weight: 600;
    font-size: 13px;
    cursor: pointer;
    transition: background 0.15s;
  }

  .send-btn:hover {
    background: #1d4ed8;
  }

  .send-btn:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  .tab-bar {
    display: flex;
    gap: 2px;
    padding: 0 4px;
    border-bottom: 1px solid #e5e7eb;
  }

  .tab-btn {
    background: transparent;
    border: none;
    color: #6b7280;
    padding: 8px 14px;
    border-bottom: 2px solid transparent;
    cursor: pointer;
    font-weight: 500;
    font-size: 13px;
    transition: color 0.15s;
  }

  .tab-btn:hover {
    color: #374151;
  }

  .tab-btn.active {
    color: #1e293b;
    border-bottom-color: #2563eb;
    font-weight: 600;
  }

  .panel {
    background: #ffffff;
    border: 1px solid #e5e7eb;
    border-radius: 10px;
    padding: 14px;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04);
  }

  .panel-inner {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .panel-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-weight: 600;
    font-size: 13px;
    color: #374151;
  }

  .panel-actions {
    display: flex;
    gap: 6px;
  }

  .ghost-btn {
    background: transparent;
    border: 1px solid #d1d5db;
    color: #374151;
    border-radius: 6px;
    padding: 4px 10px;
    cursor: pointer;
    font-size: 12px;
    transition: all 0.15s;
  }

  .ghost-btn:hover {
    background: #f3f4f6;
    border-color: #9ca3af;
  }

  .grid {
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
  }

  .grid th {
    background: #f9fafb;
    color: #6b7280;
    font-weight: 600;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }

  .grid th,
  .grid td {
    border: 1px solid #e5e7eb;
    padding: 7px 10px;
    text-align: left;
  }

  .grid tbody tr:hover {
    background: #f9fafb;
  }

  .cell-input {
    width: 100%;
    background: transparent;
    border: none;
    color: #1e293b;
    font-size: 13px;
    outline: none;
    padding: 2px 0;
  }

  .cell-input:focus {
    color: #0f172a;
  }

  .link-btn {
    background: transparent;
    border: none;
    color: #2563eb;
    cursor: pointer;
    font-size: 12px;
    padding: 2px 6px;
    border-radius: 4px;
  }

  .link-btn:hover {
    background: #eff6ff;
    color: #1d4ed8;
  }

  .body-types {
    display: flex;
    flex-wrap: wrap;
    gap: 12px;
  }

  .radio {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 13px;
    color: #374151;
    cursor: pointer;
  }

  .radio input[type="radio"] {
    accent-color: #2563eb;
  }

  .radio input[type="radio"]:disabled + span {
    color: #d1d5db;
  }

  .raw-panel {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .raw-input {
    min-height: 160px;
    background: #f9fafb;
    color: #1e293b;
    border: 1px solid #d1d5db;
    border-radius: 8px;
    padding: 12px;
    font-family: ui-monospace, "Cascadia Code", Consolas, monospace;
    font-size: 13px;
    line-height: 1.6;
    resize: vertical;
    outline: none;
  }

  .raw-input:focus {
    border-color: #3b82f6;
    box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.1);
  }

  .callout {
    padding: 10px 14px;
    border-radius: 8px;
    font-size: 13px;
    line-height: 1.5;
  }

  .callout.note {
    background: #eff6ff;
    color: #1e40af;
    border: 1px solid #bfdbfe;
  }

  .callout.warn {
    background: #fffbeb;
    color: #92400e;
    border: 1px solid #fde68a;
  }

  .response {
    background: #ffffff;
    border: 1px solid #e5e7eb;
    border-radius: 10px;
    padding: 14px;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.04);
  }

  .response-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 10px;
    flex-wrap: wrap;
    gap: 8px;
  }

  .response-meta {
    display: flex;
    gap: 12px;
    align-items: center;
  }

  .status {
    padding: 2px 10px;
    border-radius: 6px;
    background: #f3f4f6;
    font-weight: 700;
    font-size: 13px;
    color: #6b7280;
  }

  .status.ok {
    color: #16a34a;
    background: #f0fdf4;
  }

  .status.warn {
    color: #ca8a04;
    background: #fefce8;
  }

  .status.err {
    color: #dc2626;
    background: #fef2f2;
  }

  .meta {
    color: #9ca3af;
    font-size: 12px;
  }

  .view-tabs {
    display: flex;
    gap: 2px;
    margin-bottom: 8px;
    border-bottom: 1px solid #f3f4f6;
  }

  .view-btn {
    background: transparent;
    border: none;
    color: #9ca3af;
    padding: 5px 10px;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    font-size: 12px;
    font-weight: 500;
    transition: color 0.15s;
  }

  .view-btn:hover {
    color: #6b7280;
  }

  .view-btn.active {
    color: #1e293b;
    border-bottom-color: #2563eb;
    font-weight: 600;
  }

  .response-body {
    min-height: 160px;
  }

  .code {
    background: #f9fafb;
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    padding: 14px;
    font-family: ui-monospace, "Cascadia Code", Consolas, monospace;
    font-size: 13px;
    line-height: 1.6;
    color: #1e293b;
    white-space: pre-wrap;
    margin: 0;
    overflow-x: auto;
  }

  .preview {
    width: 100%;
    min-height: 200px;
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    background: #ffffff;
  }

  .err {
    margin-top: 10px;
    color: #dc2626;
    white-space: pre-wrap;
    font-size: 13px;
  }

  @media (max-width: 960px) {
    .request-bar {
      grid-template-columns: 1fr;
    }
  }
</style>

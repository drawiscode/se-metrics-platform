export class ApiError extends Error 
{
    constructor(status, message, bodyText) 
    {
      super(message)
      this.status = status
      this.bodyText = bodyText
    }
}
  
async function readTextSafe(res) 
{
    try {
      return await res.text()
    } catch {
      return ''
    }
}
//   const data = await apiGet('/api/repos')
export async function apiGet(path) 
{
    const res = await fetch(path, { method: 'GET' })
    const text = await readTextSafe(res)
    if (!res.ok) 
    {
        console.error(`API GET ${path} failed with status ${res.status}:`, text)

        throw new ApiError(res.status, `GET ${path} failed`, text)
    }
    return text ? JSON.parse(text) : {}
}

export async function apiPost(path, body = undefined, options = {}) {
  const headers = { ...(options.headers || {}) }

  let fetchBody = undefined
  if (body !== undefined) {
    // 若调用方没指定 Content-Type，则默认 JSON
    if (!headers['Content-Type'] && !headers['content-type']) {
      headers['Content-Type'] = 'application/json'
    }
    // 如果是 FormData/Blob/字符串，则交给 fetch 原样发送
    if (
      typeof body === 'string' ||
      body instanceof FormData ||
      body instanceof Blob ||
      body instanceof ArrayBuffer
    ) {
      fetchBody = body
    } else {
      fetchBody = JSON.stringify(body)
    }
  }

  const res = await fetch(path, {
    method: 'POST',
    headers,
    body: fetchBody,
    ...options,
  })

  const text = await readTextSafe(res)
  if (!res.ok) {
    console.error(`API POST ${path} failed with status ${res.status}:`, text)
    throw new ApiError(res.status, `POST ${path} failed`, text)
  }
  return text ? JSON.parse(text) : {}
}


export async function apiDelete(path, options = {}) {
  const res = await fetch(path, { method: 'DELETE', ...(options || {}) })
  const text = await readTextSafe(res)
  if (!res.ok) {
    console.error(`API DELETE ${path} failed with status ${res.status}:`, text)
    throw new ApiError(res.status, `DELETE ${path} failed`, text)
  }
  return text ? JSON.parse(text) : {}
}
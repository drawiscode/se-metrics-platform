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
        console.error(`API 66666 GET ${path} failed with status ${res.status}:`, text)

        throw new ApiError(res.status, `GET ${path} failed`, text)
    }
    return text ? JSON.parse(text) : {}
}
  
export async function apiPost(path) 
{
    const res = await fetch(path, { method: 'POST' })
    const text = await readTextSafe(res)
    if (!res.ok) 
    {
        console.error(`API 77777 POST ${path} failed with status ${res.status}:`, text)
        throw new ApiError(res.status, `POST ${path} failed`, text)
    }
    return text ? JSON.parse(text) : {}
}
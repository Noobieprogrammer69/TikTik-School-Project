import { afterEach, describe, expect, it, vi } from 'vitest'

import { ApiError, api, apiRequest } from './client'

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('apiRequest', () => {
  it('unwraps successful API data and includes cookies', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response(
      JSON.stringify({ data: { ok: true } }),
      { status: 200, headers: { 'Content-Type': 'application/json' } },
    ))
    vi.stubGlobal('fetch', fetchMock)

    await expect(apiRequest('/api/health')).resolves.toEqual({ ok: true })
    expect(fetchMock).toHaveBeenCalledWith('/api/health', expect.objectContaining({ credentials: 'include' }))
  })

  it('turns API error envelopes into useful errors', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(
      JSON.stringify({ error: { code: 'session_invalid', message: 'Please log in again' } }),
      { status: 401, headers: { 'Content-Type': 'application/json' } },
    )))

    await expect(apiRequest('/api/auth/session')).rejects.toMatchObject({
      name: 'ApiError',
      status: 401,
      code: 'session_invalid',
      message: 'Please log in again',
    })
  })

  it('reports a network failure without leaking implementation details', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('socket details')))
    await expect(apiRequest('/api/health')).rejects.toBeInstanceOf(ApiError)
    await expect(apiRequest('/api/health')).rejects.toMatchObject({ code: 'network_error' })
  })

  it('sends chat text with CSRF protection and no browser-supplied sender ID', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response(
      JSON.stringify({ data: { _id: 'message-1', text: 'Hello' } }),
      { status: 201, headers: { 'Content-Type': 'application/json' } },
    ))
    vi.stubGlobal('fetch', fetchMock)

    await api.sendMessage('user:two', 'Hello', 'csrf-value')

    const [path, request] = fetchMock.mock.calls[0]
    expect(path).toBe('/api/chat/user%3Atwo/messages')
    expect(request.method).toBe('POST')
    expect(request.headers.get('X-CSRF-Token')).toBe('csrf-value')
    expect(JSON.parse(request.body)).toEqual({ message: 'Hello' })
  })
})

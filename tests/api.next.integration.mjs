import assert from 'node:assert/strict'

const baseUrl = process.env.API_BASE_URL || 'http://127.0.0.1:8080'
const origin = process.env.FRONTEND_ORIGIN || 'http://localhost:3000'
const testKey = process.env.TEST_AUTH_KEY
if (!testKey) throw new Error('TEST_AUTH_KEY is required')

async function call(path, { method = 'GET', cookie = '', csrf = '', body, headers = {} } = {}) {
  const requestHeaders = new Headers(headers)
  if (cookie) requestHeaders.set('Cookie', cookie)
  if (csrf) requestHeaders.set('X-CSRF-Token', csrf)
  if (method !== 'GET') requestHeaders.set('Origin', origin)
  const response = await fetch(`${baseUrl}${path}`, {
    method,
    headers: { ...Object.fromEntries(requestHeaders), ...(body ? { 'Content-Type': 'application/json' } : {}) },
    body: body ? JSON.stringify(body) : undefined,
  })
  const payload = response.headers.get('content-type')?.includes('application/json') ? await response.json() : null
  return { response, payload }
}

const expectStatus = (result, status, label) => {
  assert.equal(result.response.status, status, `${label}: ${JSON.stringify(result.payload)}`)
  return result.payload?.data
}

async function login(id, name) {
  const result = await call('/api/test/login', { method: 'POST', body: { id, name }, headers: { 'X-Test-Auth-Key': testKey } })
  const data = expectStatus(result, 200, `login ${id}`)
  return { cookie: result.response.headers.get('set-cookie').split(';', 1)[0], csrf: data.csrfToken, user: data.user }
}

const suffix = Date.now().toString(36)
const alice = await login(`next-alice-${suffix}`, 'Next Alice')
const bob = await login(`next-bob-${suffix}`, 'Next Bob')

const defaults = expectStatus(await call('/api/me/privacy', { cookie: bob.cookie }), 200, 'privacy defaults')
assert.equal(defaults.privateAccount, false)
expectStatus(await call('/api/me/privacy', {
  method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
  body: { ...defaults, privateAccount: true, allowMessages: 'none', showLikedVideos: false },
}), 200, 'update privacy')

const requested = expectStatus(await call(`/api/users/${encodeURIComponent(bob.user._id)}/follow`, {
  method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { follow: true },
}), 200, 'request private follow')
assert.equal(requested.following, false)
assert.equal(requested.requested, true)

const requests = expectStatus(await call('/api/me/follow-requests', { cookie: bob.cookie }), 200, 'list follow requests')
assert.ok(requests.some((item) => item.requester._id === alice.user._id))
expectStatus(await call(`/api/me/follow-requests/${encodeURIComponent(alice.user._id)}`, {
  method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { accept: true },
}), 200, 'accept follow request')
const accepted = expectStatus(await call(`/api/users/${encodeURIComponent(bob.user._id)}/relationship`, { cookie: alice.cookie }), 200, 'accepted relationship')
assert.equal(accepted.following, true)

expectStatus(await call(`/api/chat/${encodeURIComponent(bob.user._id)}/messages`, {
  method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { message: 'privacy should block this' },
}), 403, 'message privacy')
expectStatus(await call('/api/me/privacy', {
  method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
  body: { ...defaults, privateAccount: true, allowMessages: 'everyone', showLikedVideos: false },
}), 200, 'allow messages')

const message = expectStatus(await call(`/api/chat/${encodeURIComponent(bob.user._id)}/messages`, {
  method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { message: 'searchable original message' },
}), 201, 'create message')
const edited = expectStatus(await call(`/api/chat/messages/${encodeURIComponent(message._id)}/edit`, {
  method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { message: 'searchable edited message' },
}), 200, 'edit message')
assert.equal(edited.edited, true)
assert.equal(edited.text, 'searchable edited message')
const matches = expectStatus(await call(`/api/chat/${encodeURIComponent(bob.user._id)}/search?q=edited`, { cookie: alice.cookie }), 200, 'search messages')
assert.ok(matches.some((item) => item._id === message._id))
const history = expectStatus(await call(`/api/chat/${encodeURIComponent(bob.user._id)}/history?limit=20`, { cookie: alice.cookie }), 200, 'message history')
assert.ok(history.items.some((item) => item._id === message._id))

expectStatus(await call(`/api/reports/message/${encodeURIComponent(message._id)}`, {
  method: 'POST', cookie: bob.cookie, csrf: bob.csrf, body: { reason: 'harassment', details: 'Integration report' },
}), 201, 'report message')

const analytics = expectStatus(await call('/api/me/analytics', { cookie: alice.cookie }), 200, 'creator analytics')
assert.equal(typeof analytics.totals.videos, 'number')
const sessions = expectStatus(await call('/api/me/sessions', { cookie: alice.cookie }), 200, 'session dashboard')
assert.ok(sessions.some((session) => session.current))
const events = expectStatus(await call('/api/me/security-events', { cookie: alice.cookie }), 200, 'security activity')
assert.ok(events.some((event) => event.type === 'login'))
const exported = expectStatus(await call('/api/me/export', { cookie: alice.cookie }), 200, 'account export')
assert.equal(exported.profile._id, alice.user._id)

expectStatus(await call('/api/me/account', {
  method: 'DELETE', cookie: alice.cookie, csrf: alice.csrf, body: { confirmation: 'no' },
}), 400, 'account deletion confirmation')
expectStatus(await call('/api/me/account', {
  method: 'DELETE', cookie: alice.cookie, csrf: alice.csrf, body: { confirmation: 'DELETE' },
}), 200, 'delete account')
expectStatus(await call('/api/auth/session', { cookie: alice.cookie }), 401, 'deleted account session invalid')

console.log('Next-upgrade API integration passed: privacy, follow requests, chat edit/search/history, content reports, analytics, sessions, security events, export, and deletion.')

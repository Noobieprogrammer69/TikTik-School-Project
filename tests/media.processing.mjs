import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'

const baseUrl = process.env.API_BASE_URL || 'http://127.0.0.1:8080'
const origin = process.env.FRONTEND_ORIGIN || 'http://localhost:3000'
const testKey = process.env.TEST_AUTH_KEY
const fixture = process.env.TEST_VIDEO_FILE
if (!testKey || !fixture) throw new Error('TEST_AUTH_KEY and TEST_VIDEO_FILE are required')

const login = await fetch(`${baseUrl}/api/test/login`, {
  method: 'POST', headers: { 'Content-Type': 'application/json', Origin: origin, 'X-Test-Auth-Key': testKey },
  body: JSON.stringify({ id: `media-${Date.now()}`, name: 'Media Processing Test' }),
})
assert.equal(login.status, 200)
const cookie = login.headers.get('set-cookie').split(';', 1)[0]
const session = (await login.json()).data

const form = new FormData()
form.append('video', new Blob([await readFile(fixture)], { type: 'video/webm' }), 'processing.webm')
form.append('cover', new Blob([Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Zl1sAAAAASUVORK5CYII=', 'base64')], { type: 'image/png' }), 'cover.png')
form.append('caption', 'Real FFmpeg processing check')
form.append('topic', 'Gaming')
const uploaded = await fetch(`${baseUrl}/api/videos`, {
  method: 'POST', headers: { Cookie: cookie, Origin: origin, 'X-CSRF-Token': session.csrfToken }, body: form,
})
const uploadedPayload = await uploaded.json()
assert.equal(uploaded.status, 201, JSON.stringify(uploadedPayload))
const post = uploadedPayload.data
assert.deepEqual([...post.video.asset.qualities].sort(), ['360', '720'])
assert.match(post.thumbnailUrl, /^\/api\/media\//)

for (const quality of post.video.asset.qualities) {
  const response = await fetch(`${baseUrl}${post.video.asset.url}?quality=${quality}`, {
    headers: { Cookie: cookie, Range: 'bytes=0-99' },
  })
  assert.equal(response.status, 206, `${quality}p range response`)
  assert.equal(response.headers.get('content-type'), 'video/mp4')
  assert.equal((await response.arrayBuffer()).byteLength, 100)
}

const cover = await fetch(`${baseUrl}${post.thumbnailUrl}`, { headers: { Cookie: cookie } })
assert.equal(cover.status, 200)
assert.equal(cover.headers.get('content-type'), 'image/png')

const removed = await fetch(`${baseUrl}/api/posts/${encodeURIComponent(post._id)}`, {
  method: 'DELETE', headers: { Cookie: cookie, Origin: origin, 'X-CSRF-Token': session.csrfToken },
})
assert.equal(removed.status, 200)
console.log('Media processing passed: custom cover, 360p/720p FFmpeg variants, byte ranges, and cleanup route.')

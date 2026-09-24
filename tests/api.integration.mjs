import assert from 'node:assert/strict'

const baseUrl = process.env.API_BASE_URL || 'http://127.0.0.1:8080'
const origin = process.env.FRONTEND_ORIGIN || 'http://localhost:3000'
const testKey = process.env.TEST_AUTH_KEY

if (!testKey) throw new Error('TEST_AUTH_KEY is required')

async function call(path, { method = 'GET', cookie = '', csrf = '', body, headers = {} } = {}) {
  const requestHeaders = new Headers(headers)
  if (cookie) requestHeaders.set('Cookie', cookie)
  if (csrf) requestHeaders.set('X-CSRF-Token', csrf)
  if (method !== 'GET' && !requestHeaders.has('Origin')) requestHeaders.set('Origin', origin)
  let requestBody = body
  if (body && !(body instanceof FormData)) {
    requestHeaders.set('Content-Type', 'application/json')
    requestBody = JSON.stringify(body)
  }
  const response = await fetch(`${baseUrl}${path}`, { method, headers: requestHeaders, body: requestBody })
  const contentType = response.headers.get('content-type') || ''
  const payload = contentType.includes('application/json') ? await response.json() : null
  return { response, payload }
}

function expectStatus(result, expected, label) {
  assert.equal(result.response.status, expected, `${label}: ${JSON.stringify(result.payload)}`)
  return result.payload?.data
}

async function login(id, name) {
  const result = await call('/api/test/login', {
    method: 'POST',
    body: { id, name },
    headers: { 'X-Test-Auth-Key': testKey },
  })
  const data = expectStatus(result, 200, `login ${id}`)
  const setCookie = result.response.headers.get('set-cookie')
  assert.ok(setCookie?.includes('tt_session='), 'login must set the server session cookie')
  assert.match(setCookie, /HttpOnly/i)
  assert.match(setCookie, /SameSite=Lax/i)
  return { cookie: setCookie.split(';', 1)[0], csrf: data.csrfToken, user: data.user }
}

console.log(`API integration target: ${baseUrl}`)
expectStatus(await call('/api/health'), 200, 'health')
expectStatus(await call('/api/auth/session'), 401, 'anonymous session is protected')
expectStatus(await call('/api/videos', { method: 'POST', body: new FormData() }), 401, 'upload is protected')
expectStatus(await call('/api/chat/conversations'), 401, 'chat is protected')
expectStatus(await call('/api/notifications'), 401, 'notifications are protected')
expectStatus(
  await call('/api/auth/google', { method: 'POST', body: { credential: 'not-a-jwt' } }),
  401,
  'invalid or unconfigured Google login is rejected',
)

const wrongKey = await call('/api/test/login', {
  method: 'POST',
  body: { id: 'wrong', name: 'Wrong' },
  headers: { 'X-Test-Auth-Key': 'not-the-key' },
})
expectStatus(wrongKey, 403, 'test auth rejects wrong key')

const alice = await login('api-alice', 'API Alice')
assert.equal(alice.user._id, 'test:api-alice')

const restored = expectStatus(
  await call('/api/auth/session', { cookie: alice.cookie }),
  200,
  'session restoration',
)
alice.csrf = restored.csrfToken
assert.equal(restored.user._id, alice.user._id)

const updatedProfile = expectStatus(
  await call('/api/me/profile', {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf,
    body: { userName: 'API Alice Updated', bio: 'Testing all twenty upgrades' },
  }),
  200,
  'edit profile',
)
assert.equal(updatedProfile.userName, 'API Alice Updated')
assert.equal(updatedProfile.bio, 'Testing all twenty upgrades')
assert.equal(expectStatus(
  await call('/api/me/profile', { cookie: alice.cookie }),
  200,
  'read own profile',
).bio, 'Testing all twenty upgrades')

const pngBytes = Buffer.from(
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Zl1sAAAAASUVORK5CYII=',
  'base64',
)
const avatarForm = new FormData()
avatarForm.append('avatar', new Blob([pngBytes], { type: 'image/png' }), 'avatar.png')
const avatarUser = expectStatus(
  await call('/api/me/avatar', {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: avatarForm,
  }),
  200,
  'upload profile image',
)
assert.match(avatarUser.image, /^\/api\/avatars\//)
assert.equal((await call(avatarUser.image, { cookie: alice.cookie })).response.status, 200)

expectStatus(
  await call('/api/auth/logout', { method: 'POST', cookie: alice.cookie }),
  403,
  'mutation without CSRF is rejected',
)
expectStatus(
  await call('/api/auth/logout', {
    method: 'POST',
    cookie: alice.cookie,
    csrf: alice.csrf,
    headers: { Origin: 'https://attacker.example' },
  }),
  403,
  'foreign origin is rejected',
)

const invalidForm = new FormData()
invalidForm.append('caption', 'Bad video')
invalidForm.append('topic', 'Gaming')
invalidForm.append('video', new Blob(['not a video'], { type: 'video/mp4' }), 'bad.mp4')
expectStatus(
  await call('/api/videos', { method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: invalidForm }),
  415,
  'invalid upload is rejected',
)

const uniqueCaption = `API persistence ${Date.now()} #cpp`
// Cross the backend's 16 MiB in-memory threshold so this also exercises
// Drogon's temporary-file-backed multipart path.
const videoBytes = new Uint8Array(17 * 1024 * 1024)
videoBytes.set([
  0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70,
  0x69, 0x73, 0x6f, 0x6d, 0x00, 0x00, 0x02, 0x00,
])
const uploadForm = new FormData()
uploadForm.append('caption', uniqueCaption)
uploadForm.append('topic', 'Gaming')
uploadForm.append('video', new Blob([videoBytes], { type: 'video/mp4' }), 'integration.mp4')
const post = expectStatus(
  await call('/api/videos', { method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: uploadForm }),
  201,
  'valid upload',
)
assert.equal(post.userId, alice.user._id)
expectStatus(
  await call(`/api/posts/${post._id}/comments`, { method: 'POST', body: { comment: 'anonymous' } }),
  401,
  'comments are protected',
)

const feed = expectStatus(await call('/api/posts?topic=Gaming'), 200, 'topic feed')
assert.ok(feed.some((item) => item._id === post._id))
expectStatus(await call(`/api/posts/${post._id}`), 200, 'direct post route')
const upgradedFeed = expectStatus(
  await call('/api/feed?mode=for_you&hashtag=cpp&limit=1', { cookie: alice.cookie }),
  200,
  'recommended hashtag feed',
)
assert.equal(upgradedFeed.items[0]._id, post._id)
assert.ok(upgradedFeed.items[0].recommendationReason)
expectStatus(
  await call('/api/feed?mode=following'),
  401,
  'following feed requires login',
)

expectStatus(
  await call(`/api/posts/${post._id}/save`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { saved: true },
  }),
  200,
  'save video',
)
assert.ok(expectStatus(
  await call('/api/me/saved', { cookie: alice.cookie }), 200, 'saved library',
).some((item) => item._id === post._id))
const firstView = expectStatus(
  await call(`/api/posts/${post._id}/view`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { watchSeconds: 3 },
  }),
  200,
  'record view',
)
const repeatedView = expectStatus(
  await call(`/api/posts/${post._id}/view`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { watchSeconds: 8 },
  }),
  200,
  'repeat view increments count while keeping one history item',
)
assert.equal(repeatedView.viewCount, firstView.viewCount + 1)
const anonymousView = expectStatus(
  await call(`/api/posts/${post._id}/view`, {
    method: 'POST', body: { watchSeconds: 0 },
  }),
  200,
  'anonymous view increments count without creating account history',
)
assert.equal(anonymousView.viewCount, repeatedView.viewCount + 1)
assert.ok(expectStatus(
  await call('/api/me/history', { cookie: alice.cookie }), 200, 'view history',
).some((item) => item._id === post._id))
const shared = expectStatus(
  await call(`/api/posts/${post._id}/share`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf,
  }),
  200,
  'record share',
)
assert.ok(shared.shareCount >= 1)

const bob = await login('api-bob', 'API Bob')

const sharedByBob = expectStatus(
  await call(`/api/posts/${post._id}/share`, {
    method: 'POST', cookie: bob.cookie, csrf: bob.csrf,
  }),
  200,
  'sharing another user video creates activity',
)
assert.ok(sharedByBob.shareCount >= 2)

const relationship = expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/relationship`, {
    cookie: alice.cookie,
  }),
  200,
  'relationship status',
)
expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/follow`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { follow: true },
  }),
  200,
  'follow account',
)
const repeatedFollow = expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/follow`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { follow: true },
  }),
  200,
  'follow is idempotent',
)
assert.equal(repeatedFollow.followerCount, relationship.followerCount + (relationship.following ? 0 : 1))
expectStatus(
  await call(`/api/users/${encodeURIComponent(alice.user._id)}/follow`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { follow: true },
  }),
  409,
  'self follow is rejected',
)

const report = expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/report`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf,
    body: { reason: 'spam', details: 'API test report' },
  }),
  201,
  'report account',
)
const repeatedReport = expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/report`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf,
    body: { reason: 'other', details: 'Updated API test report' },
  }),
  201,
  'repeated report updates without duplication',
)
assert.equal(repeatedReport.reportId, report.reportId)

const followedUploadBytes = new Uint8Array([
  0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70,
  0x69, 0x73, 0x6f, 0x6d, 0x00, 0x00, 0x02, 0x00,
])
const followedUploadForm = new FormData()
followedUploadForm.append('caption', `Followed creator upload ${Date.now()}`)
followedUploadForm.append('topic', 'Gaming')
followedUploadForm.append('video', new Blob([followedUploadBytes], { type: 'video/mp4' }), 'followed.mp4')
const followedPost = expectStatus(
  await call('/api/videos', {
    method: 'POST', cookie: bob.cookie, csrf: bob.csrf, body: followedUploadForm,
  }),
  201,
  'upload notifies followers',
)
const followingFeed = expectStatus(
  await call('/api/feed?mode=following&limit=10', { cookie: alice.cookie }),
  200,
  'following feed',
)
assert.ok(followingFeed.items.some((item) => item._id === followedPost._id))
assert.ok(followingFeed.items.every((item) => item.userId === bob.user._id))

expectStatus(
  await call(`/api/chat/${encodeURIComponent(bob.user._id)}/messages`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { message: '   ' },
  }),
  400,
  'blank message is rejected',
)
expectStatus(
  await call(`/api/chat/${encodeURIComponent(alice.user._id)}/messages`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { message: 'self' },
  }),
  409,
  'self messaging is rejected',
)

// Clear unread messages from earlier repeatable test runs, then exercise both directions.
expectStatus(
  await call(`/api/chat/${encodeURIComponent(alice.user._id)}/read`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
  }),
  200,
  'mark earlier chat messages read',
)
const chatText = `Hello Bob ${Date.now()}`
const sentMessage = expectStatus(
  await call(`/api/chat/${encodeURIComponent(bob.user._id)}/messages`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { message: chatText },
  }),
  201,
  'send private message',
)
assert.equal(sentMessage.senderId, alice.user._id)
assert.equal(sentMessage.recipientId, bob.user._id)
const reactedMessage = expectStatus(
  await call(`/api/chat/messages/${sentMessage._id}/reaction`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { emoji: 'heart' },
  }),
  200,
  'react to message',
)
assert.deepEqual(reactedMessage.reactions, [{ userId: bob.user._id, emoji: 'heart' }])

const bobConversations = expectStatus(
  await call('/api/chat/conversations', { cookie: bob.cookie }),
  200,
  'list conversations',
)
const aliceConversation = bobConversations.find((item) => item.user._id === alice.user._id)
assert.ok(aliceConversation)
assert.equal(aliceConversation.lastMessage.text, chatText)
assert.ok(aliceConversation.unreadCount >= 1)

const bobMessages = expectStatus(
  await call(`/api/chat/${encodeURIComponent(alice.user._id)}/messages`, { cookie: bob.cookie }),
  200,
  'read conversation messages',
)
assert.ok(bobMessages.some((item) => item.text === chatText))
const markedRead = expectStatus(
  await call(`/api/chat/${encodeURIComponent(alice.user._id)}/read`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
  }),
  200,
  'mark conversation read',
)
assert.ok(markedRead.updatedCount >= 1)
const readConversations = expectStatus(
  await call('/api/chat/conversations', { cookie: bob.cookie }),
  200,
  'unread state is persisted',
)
assert.equal(readConversations.find((item) => item.user._id === alice.user._id).unreadCount, 0)

const replyText = `Hello Alice ${Date.now()}`
const replyMessage = expectStatus(
  await call(`/api/chat/${encodeURIComponent(alice.user._id)}/messages`, {
    method: 'POST', cookie: bob.cookie, csrf: bob.csrf,
    body: { message: replyText, replyTo: sentMessage._id },
  }),
  201,
  'reply to a specific private message',
)
assert.equal(replyMessage.replyTo, sentMessage._id)
const aliceMessages = expectStatus(
  await call(`/api/chat/${encodeURIComponent(bob.user._id)}/messages`, { cookie: alice.cookie }),
  200,
  'other participant reads the same conversation',
)
assert.ok(aliceMessages.some((item) => item.text === chatText))
assert.ok(aliceMessages.some((item) => item.text === replyText))

const attachmentForm = new FormData()
attachmentForm.append('message', 'An image attachment')
attachmentForm.append('attachment', new Blob([pngBytes], { type: 'image/png' }), 'chat.png')
const attachmentMessage = expectStatus(
  await call(`/api/chat/${encodeURIComponent(bob.user._id)}/attachments`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: attachmentForm,
  }),
  201,
  'send chat attachment',
)
assert.equal(attachmentMessage.attachment.contentType, 'image/png')
assert.equal((await call(attachmentMessage.attachment.url, { cookie: bob.cookie })).response.status, 200)
expectStatus(
  await call(`/api/chat/${encodeURIComponent(alice.user._id)}/settings`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
    body: { muted: true, archived: true },
  }),
  200,
  'mute and archive conversation',
)
const configuredConversation = expectStatus(
  await call('/api/chat/conversations', { cookie: bob.cookie }),
  200,
  'conversation settings persist',
).find((item) => item.user._id === alice.user._id)
assert.equal(configuredConversation.muted, true)
assert.equal(configuredConversation.archived, true)
expectStatus(
  await call(`/api/chat/${encodeURIComponent(alice.user._id)}/settings`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
    body: { muted: false, archived: false },
  }),
  200,
  'restore conversation notifications',
)
expectStatus(
  await call(`/api/chat/messages/${attachmentMessage._id}`, {
    method: 'DELETE', cookie: bob.cookie, csrf: bob.csrf,
  }),
  403,
  'only the message sender can delete',
)
const deletedAttachmentMessage = expectStatus(
  await call(`/api/chat/messages/${attachmentMessage._id}`, {
    method: 'DELETE', cookie: alice.cookie, csrf: alice.csrf,
  }),
  200,
  'sender deletes message and attachment',
)
assert.equal(deletedAttachmentMessage.deleted, true)
expectStatus(await call(attachmentMessage.attachment.url, { cookie: alice.cookie }), 404, 'deleted attachment is unavailable')

expectStatus(
  await call(`/api/posts/${post._id}/like`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { like: true },
  }),
  200,
  'like',
)
const liked = expectStatus(
  await call(`/api/posts/${post._id}/like`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { like: true },
  }),
  200,
  'idempotent like',
)
assert.equal(liked.likes.filter((item) => item._ref === bob.user._id).length, 1)

const commented = expectStatus(
  await call(`/api/posts/${post._id}/comments`, {
    method: 'POST', cookie: bob.cookie, csrf: bob.csrf, body: { comment: 'API comment' },
  }),
  200,
  'comment',
)
assert.ok(commented.comments.some((item) => item.comment === 'API comment'))
const apiComment = commented.comments.find((item) => item.comment === 'API comment')

const commentLiked = expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/like`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { like: true },
  }),
  200,
  'like comment',
)
assert.equal(commentLiked.comments.find((item) => item._key === apiComment._key).likes.length, 1)
const replied = expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/replies`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf, body: { reply: 'API reply' },
  }),
  200,
  'reply to comment',
)
assert.ok(replied.comments.find((item) => item._key === apiComment._key).replies
  .some((item) => item.comment === 'API reply'))
const apiReply = replied.comments.find((item) => item._key === apiComment._key).replies
  .find((item) => item.comment === 'API reply')
expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { comment: 'Not mine' },
  }),
  403,
  'only the comment author can edit',
)
const editedCommentPost = expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { comment: 'API comment edited' },
  }),
  200,
  'edit own comment',
)
assert.equal(editedCommentPost.comments.find((item) => item._key === apiComment._key).comment, 'API comment edited')
expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/replies/${apiReply._key}`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { reply: 'Not mine' },
  }),
  403,
  'only the reply author can edit',
)
const editedReplyPost = expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/replies/${apiReply._key}`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { reply: 'API reply edited' },
  }),
  200,
  'edit own reply',
)
assert.equal(
  editedReplyPost.comments.find((item) => item._key === apiComment._key).replies
    .find((item) => item._key === apiReply._key).comment,
  'API reply edited',
)
expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/pin`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { pinned: true },
  }),
  403,
  'non-owner comment pin is rejected',
)
const pinned = expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/pin`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { pinned: true },
  }),
  200,
  'owner pins comment',
)
assert.equal(pinned.comments.find((item) => item._key === apiComment._key).pinned, true)

const bobNotifications = expectStatus(
  await call('/api/notifications', { cookie: bob.cookie }),
  200,
  'notifications include social activity',
)
assert.ok(bobNotifications.some((item) => item.type === 'follow' && item.actor._id === alice.user._id))
assert.ok(bobNotifications.some((item) => item.type === 'message' && item.actor._id === alice.user._id))
assert.ok(bobNotifications.some((item) => item.type === 'comment_like' && item.commentId === apiComment._key))
assert.ok(bobNotifications.some((item) => item.type === 'reply' && item.commentId === apiComment._key))
assert.ok(bobNotifications.some((item) => item.type === 'comment_pin' && item.commentId === apiComment._key))
const oneUnread = bobNotifications.find((item) => !item.read)
if (oneUnread) {
  const markedOne = expectStatus(
    await call(`/api/notifications/${oneUnread._id}`, {
      method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
    }),
    200,
    'mark one notification read',
  )
  assert.equal(markedOne.read, 1)
}
const defaultPreferences = expectStatus(
  await call('/api/notification-preferences', { cookie: bob.cookie }),
  200,
  'read notification preferences',
)
assert.equal(defaultPreferences.message, true)
assert.equal(defaultPreferences.post_share, true)
const messageNotificationsBeforeMute = bobNotifications.filter((item) => item.type === 'message').length
const mutedPreferences = expectStatus(
  await call('/api/notification-preferences', {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
    body: { ...defaultPreferences, message: false },
  }),
  200,
  'disable message notifications',
)
assert.equal(mutedPreferences.message, false)
expectStatus(
  await call(`/api/chat/${encodeURIComponent(bob.user._id)}/messages`, {
    method: 'POST', cookie: alice.cookie, csrf: alice.csrf,
    body: { message: `Muted notification ${Date.now()}` },
  }),
  201,
  'send while message notifications are disabled',
)
assert.equal(
  expectStatus(await call('/api/notifications', { cookie: bob.cookie }), 200, 'disabled notification stays absent')
    .filter((item) => item.type === 'message').length,
  messageNotificationsBeforeMute,
)
expectStatus(
  await call('/api/notification-preferences', {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
    body: { ...defaultPreferences, message: true },
  }),
  200,
  'restore message notifications',
)
const removableNotification = bobNotifications.find((item) => item.type === 'reply')
if (removableNotification) {
  assert.equal(expectStatus(
    await call(`/api/notifications/${removableNotification._id}`, {
      method: 'DELETE', cookie: bob.cookie, csrf: bob.csrf,
    }),
    200,
    'delete one notification',
  ).deleted, 1)
  assert.ok(!expectStatus(
    await call('/api/notifications', { cookie: bob.cookie }),
    200,
    'deleted notification stays absent',
  ).some((item) => item._id === removableNotification._id))
}
const aliceNotifications = expectStatus(
  await call('/api/notifications', { cookie: alice.cookie }),
  200,
  'video, comment, message, and followed-upload notifications',
)
assert.ok(aliceNotifications.some((item) => item.type === 'new_post' && item.postId === followedPost._id))
assert.ok(aliceNotifications.some((item) => item.type === 'post_like' && item.postId === post._id))
assert.ok(aliceNotifications.some((item) => item.type === 'comment' && item.postId === post._id))
assert.ok(aliceNotifications.some((item) => item.type === 'post_share' && item.postId === post._id && item.actor._id === bob.user._id))
assert.ok(aliceNotifications.some((item) => item.type === 'message' && item.actor._id === bob.user._id))
expectStatus(
  await call('/api/notifications/read', {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf,
  }),
  200,
  'mark notifications read',
)
assert.ok(expectStatus(
  await call('/api/notifications', { cookie: bob.cookie }),
  200,
  'notification read state persists',
).every((item) => item.read))

expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/pin`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { pinned: false },
  }),
  200,
  'owner unpins comment',
)
expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/like`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { like: false },
  }),
  200,
  'unlike comment',
)
expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/follow`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { follow: false },
  }),
  200,
  'unfollow account',
)
const bobNotificationsAfterUndo = expectStatus(
  await call('/api/notifications', { cookie: bob.cookie }),
  200,
  'undo removes state notifications',
)
assert.ok(!bobNotificationsAfterUndo.some((item) => item.type === 'follow' && item.actor._id === alice.user._id))
assert.ok(!bobNotificationsAfterUndo.some((item) => item.type === 'comment_like' && item.commentId === apiComment._key))
assert.ok(!bobNotificationsAfterUndo.some((item) => item.type === 'comment_pin' && item.commentId === apiComment._key))

expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/replies/${apiReply._key}`, {
    method: 'DELETE', cookie: bob.cookie, csrf: bob.csrf,
  }),
  403,
  'non-owner cannot delete another user reply',
)
const withoutReply = expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}/replies/${apiReply._key}`, {
    method: 'DELETE', cookie: alice.cookie, csrf: alice.csrf,
  }),
  200,
  'reply author deletes reply',
)
assert.equal(withoutReply.comments.find((item) => item._key === apiComment._key).replies.length, 0)
const withoutComment = expectStatus(
  await call(`/api/posts/${post._id}/comments/${apiComment._key}`, {
    method: 'DELETE', cookie: alice.cookie, csrf: alice.csrf,
  }),
  200,
  'video owner hides or removes comment',
)
assert.ok(!withoutComment.comments.some((item) => item._key === apiComment._key))

const profile = expectStatus(await call(`/api/profiles/${encodeURIComponent(bob.user._id)}`), 200, 'profile')
assert.ok(profile.userLikedVideos.some((item) => item._id === post._id))
expectStatus(
  await call(`/api/posts/${post._id}/like`, {
    method: 'PUT', cookie: bob.cookie, csrf: bob.csrf, body: { like: false },
  }),
  200,
  'unlike video',
)
assert.ok(!expectStatus(
  await call('/api/notifications', { cookie: alice.cookie }),
  200,
  'unlike removes video-like notification',
).some((item) => item.type === 'post_like' && item.postId === post._id))
const search = expectStatus(await call(`/api/search?q=${encodeURIComponent(uniqueCaption)}`), 200, 'search')
assert.ok(search.videos.some((item) => item._id === post._id))
const accountSearch = expectStatus(await call('/api/search?q=API%20Bob'), 200, 'account search')
assert.ok(accountSearch.users.some((item) => item._id === bob.user._id))

const initialSafety = expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/safety`, { cookie: alice.cookie }),
  200,
  'read account safety state',
)
assert.equal(initialSafety.blocked, false)
expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/mute`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { muted: true },
  }),
  200,
  'mute account',
)
const mutedFeed = expectStatus(
  await call('/api/feed?mode=for_you&limit=50', { cookie: alice.cookie }),
  200,
  'muted creator is filtered from feed',
)
assert.ok(!mutedFeed.items.some((item) => item.userId === bob.user._id))
expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/mute`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { muted: false },
  }),
  200,
  'unmute account',
)
expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/block`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { blocked: true },
  }),
  200,
  'block account',
)
expectStatus(
  await call(`/api/chat/${encodeURIComponent(bob.user._id)}/messages`, { cookie: alice.cookie }),
  403,
  'blocking closes conversation access',
)
expectStatus(
  await call(`/api/users/${encodeURIComponent(bob.user._id)}/block`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { blocked: false },
  }),
  200,
  'unblock account',
)
expectStatus(
  await call('/api/admin/stats', { cookie: bob.cookie }),
  403,
  'non-admin cannot access moderation statistics',
)

expectStatus(
  await call(`/api/posts/${post._id}/save`, {
    method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { saved: false },
  }),
  200,
  'unsave video',
)
assert.ok(!expectStatus(
  await call('/api/me/saved', { cookie: alice.cookie }), 200, 'unsaved video leaves library',
).some((item) => item._id === post._id))

expectStatus(
  await call(`/api/posts/${post._id}`, { method: 'DELETE', cookie: bob.cookie, csrf: bob.csrf }),
  403,
  'non-owner deletion',
)

const range = await call(`/api/media/${post.video.asset._id}`, { headers: { Range: 'bytes=4-7' } })
assert.equal(range.response.status, 206)
assert.equal(range.response.headers.get('accept-ranges'), 'bytes')
assert.equal(range.response.headers.get('content-range'), `bytes 4-7/${videoBytes.length}`)
assert.equal(Buffer.from(await range.response.arrayBuffer()).toString('ascii'), 'ftyp')

const badRange = await call(`/api/media/${post.video.asset._id}`, {
  headers: { Range: `bytes=${videoBytes.length + 1}-${videoBytes.length + 2}` },
})
expectStatus(badRange, 416, 'invalid media range')

expectStatus(
  await call(`/api/posts/${post._id}`, { method: 'DELETE', cookie: alice.cookie, csrf: alice.csrf }),
  200,
  'owner deletion',
)
expectStatus(await call(`/api/media/${post.video.asset._id}`), 404, 'deleted media metadata')

expectStatus(
  await call(`/api/posts/${followedPost._id}`, { method: 'DELETE', cookie: bob.cookie, csrf: bob.csrf }),
  200,
  'followed creator deletes test upload',
)

expectStatus(
  await call('/api/auth/logout', { method: 'POST', cookie: bob.cookie, csrf: bob.csrf }),
  200,
  'logout',
)
expectStatus(await call('/api/auth/session', { cookie: bob.cookie }), 401, 'logout invalidates session')

if (restored.user.isAdmin) {
  const stats = expectStatus(
    await call('/api/admin/stats', { cookie: alice.cookie }),
    200,
    'admin statistics',
  )
  assert.ok(stats.users >= 2)
  assert.ok(stats.messages >= 1)
  const openReports = expectStatus(
    await call('/api/admin/reports?status=open', { cookie: alice.cookie }),
    200,
    'admin report queue',
  )
  assert.ok(openReports.some((item) => item._id === report.reportId))
  expectStatus(
    await call(`/api/admin/reports/${report.reportId}`, {
      method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { status: 'resolved' },
    }),
    200,
    'resolve report',
  )

  const charlie = await login(`api-charlie-${Date.now()}`, 'API Charlie')
  expectStatus(
    await call(`/api/admin/users/${encodeURIComponent(charlie.user._id)}/suspension`, {
      method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { suspended: true },
    }),
    200,
    'suspend account',
  )
  expectStatus(
    await call('/api/auth/session', { cookie: charlie.cookie }),
    401,
    'suspension invalidates active sessions',
  )
  expectStatus(
    await call('/api/test/login', {
      method: 'POST', body: { id: charlie.user._id.slice(5), name: 'API Charlie' },
      headers: { 'X-Test-Auth-Key': testKey },
    }),
    403,
    'suspended account cannot log in',
  )
  expectStatus(
    await call(`/api/admin/users/${encodeURIComponent(charlie.user._id)}/suspension`, {
      method: 'PUT', cookie: alice.cookie, csrf: alice.csrf, body: { suspended: false },
    }),
    200,
    'restore account',
  )
} else {
  console.warn('Admin success-path checks skipped: configure ADMIN_USER_IDS=test:api-alice')
}

console.log('API integration passed: auth, profiles/avatars, feeds/hashtags, library/history/shares, safety, notifications/preferences, realtime-ready chat features, uploads/media, comments/replies, moderation authorization, search, ownership, deletion, and logout.')

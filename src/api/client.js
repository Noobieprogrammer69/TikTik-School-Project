export class ApiError extends Error {
  constructor(message, status, code = 'request_failed') {
    super(message)
    this.name = 'ApiError'
    this.status = status
    this.code = code
  }
}

export async function apiRequest(path, options = {}) {
  const headers = new Headers(options.headers || {})
  let body = options.body
  if (body && !(body instanceof FormData) && typeof body !== 'string') {
    headers.set('Content-Type', 'application/json')
    body = JSON.stringify(body)
  }
  if (options.csrfToken) {
    headers.set('X-CSRF-Token', options.csrfToken)
  }

  let response
  try {
    response = await fetch(path, {
      ...options,
      body,
      headers,
      credentials: 'include',
    })
  } catch {
    throw new ApiError('Cannot reach the application server. Check that the backend is running.', 0, 'network_error')
  }

  const contentType = response.headers.get('content-type') || ''
  const payload = contentType.includes('application/json') ? await response.json() : null
  if (!response.ok) {
    throw new ApiError(
      payload?.error?.message || `Request failed with status ${response.status}`,
      response.status,
      payload?.error?.code,
    )
  }
  return payload?.data
}

export function uploadRequest(path, form, csrfToken, { onProgress, signal } = {}) {
  return new Promise((resolve, reject) => {
    const request = new window.XMLHttpRequest()
    request.open('POST', path)
    request.withCredentials = true
    if (csrfToken) request.setRequestHeader('X-CSRF-Token', csrfToken)
    request.upload.addEventListener('progress', (event) => {
      if (event.lengthComputable) onProgress?.(Math.round((event.loaded / event.total) * 100))
    })
    request.addEventListener('load', () => {
      let payload = null
      try { payload = request.responseText ? JSON.parse(request.responseText) : null } catch { /* ignored */ }
      if (request.status >= 200 && request.status < 300) resolve(payload?.data)
      else reject(new ApiError(payload?.error?.message || `Request failed with status ${request.status}`, request.status, payload?.error?.code))
    })
    request.addEventListener('error', () => reject(new ApiError('Cannot reach the application server.', 0, 'network_error')))
    request.addEventListener('abort', () => reject(new ApiError('Upload canceled.', 0, 'aborted')))
    signal?.addEventListener('abort', () => request.abort(), { once: true })
    request.send(form)
  })
}

export const api = {
  publicConfig: () => apiRequest('/api/config/public'),
  session: () => apiRequest('/api/auth/session'),
  googleLogin: (credential) => apiRequest('/api/auth/google', { method: 'POST', body: { credential } }),
  logout: (csrfToken) => apiRequest('/api/auth/logout', { method: 'POST', csrfToken }),
  users: () => apiRequest('/api/users'),
  posts: (topic) => apiRequest(`/api/posts${topic ? `?topic=${encodeURIComponent(topic)}` : ''}`),
  feed: ({ mode = 'for_you', topic = '', hashtag = '', cursor = '', limit = 12 } = {}) => {
    const params = new window.URLSearchParams({ mode, limit: String(limit) })
    if (topic) params.set('topic', topic)
    if (hashtag) params.set('hashtag', hashtag)
    if (cursor) params.set('cursor', cursor)
    return apiRequest(`/api/feed?${params}`)
  },
  post: (id) => apiRequest(`/api/posts/${encodeURIComponent(id)}`),
  profile: (id) => apiRequest(`/api/profiles/${encodeURIComponent(id)}`),
  myProfile: () => apiRequest('/api/me/profile'),
  updateProfile: (userName, bio, csrfToken) =>
    apiRequest('/api/me/profile', { method: 'PUT', body: { userName, bio }, csrfToken }),
  updateAvatar: (form, csrfToken) =>
    apiRequest('/api/me/avatar', { method: 'POST', body: form, csrfToken }),
  relationship: (id) => apiRequest(`/api/users/${encodeURIComponent(id)}/relationship`),
  setFollow: (id, follow, csrfToken) =>
    apiRequest(`/api/users/${encodeURIComponent(id)}/follow`, {
      method: 'PUT', body: { follow }, csrfToken,
    }),
  reportUser: (id, reason, details, csrfToken) =>
    apiRequest(`/api/users/${encodeURIComponent(id)}/report`, {
      method: 'POST', body: { reason, details }, csrfToken,
    }),
  safety: (id) => apiRequest(`/api/users/${encodeURIComponent(id)}/safety`),
  setBlocked: (id, blocked, csrfToken) =>
    apiRequest(`/api/users/${encodeURIComponent(id)}/block`, {
      method: 'PUT', body: { blocked }, csrfToken,
    }),
  setMuted: (id, muted, csrfToken) =>
    apiRequest(`/api/users/${encodeURIComponent(id)}/mute`, {
      method: 'PUT', body: { muted }, csrfToken,
    }),
  search: (term) => apiRequest(`/api/search?q=${encodeURIComponent(term)}`),
  conversations: () => apiRequest('/api/chat/conversations'),
  messages: (userId) => apiRequest(`/api/chat/${encodeURIComponent(userId)}/messages`),
  messageHistory: (userId, before = '', limit = 50) => apiRequest(`/api/chat/${encodeURIComponent(userId)}/history?before=${encodeURIComponent(before)}&limit=${limit}`),
  searchMessages: (userId, term) => apiRequest(`/api/chat/${encodeURIComponent(userId)}/search?q=${encodeURIComponent(term)}`),
  sendMessage: (userId, message, csrfToken, replyTo = '') =>
    apiRequest(`/api/chat/${encodeURIComponent(userId)}/messages`, {
      method: 'POST',
      body: replyTo ? { message, replyTo } : { message },
      csrfToken,
    }),
  sendAttachment: (userId, form, csrfToken) =>
    apiRequest(`/api/chat/${encodeURIComponent(userId)}/attachments`, {
      method: 'POST', body: form, csrfToken,
    }),
  reactToMessage: (messageId, emoji, csrfToken) =>
    apiRequest(`/api/chat/messages/${encodeURIComponent(messageId)}/reaction`, {
      method: 'PUT', body: { emoji }, csrfToken,
    }),
  deleteMessage: (messageId, csrfToken) =>
    apiRequest(`/api/chat/messages/${encodeURIComponent(messageId)}`, {
      method: 'DELETE', csrfToken,
    }),
  editMessage: (messageId, message, csrfToken) =>
    apiRequest(`/api/chat/messages/${encodeURIComponent(messageId)}/edit`, {
      method: 'PUT', body: { message }, csrfToken,
    }),
  conversationSettings: (userId, muted, archived, csrfToken) =>
    apiRequest(`/api/chat/${encodeURIComponent(userId)}/settings`, {
      method: 'PUT', body: { muted, archived }, csrfToken,
    }),
  presence: (userId) => apiRequest(`/api/users/${encodeURIComponent(userId)}/presence`),
  markConversationRead: (userId, csrfToken) =>
    apiRequest(`/api/chat/${encodeURIComponent(userId)}/read`, {
      method: 'PUT',
      csrfToken,
    }),
  setLike: (id, like, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(id)}/like`, {
      method: 'PUT',
      body: { like },
      csrfToken,
    }),
  addComment: (id, comment, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(id)}/comments`, {
      method: 'POST',
      body: { comment },
      csrfToken,
    }),
  setCommentLike: (postId, commentId, like, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(postId)}/comments/${encodeURIComponent(commentId)}/like`, {
      method: 'PUT', body: { like }, csrfToken,
    }),
  addReply: (postId, commentId, reply, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(postId)}/comments/${encodeURIComponent(commentId)}/replies`, {
      method: 'POST', body: { reply }, csrfToken,
    }),
  editComment: (postId, commentId, comment, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(postId)}/comments/${encodeURIComponent(commentId)}`, {
      method: 'PUT', body: { comment }, csrfToken,
    }),
  deleteComment: (postId, commentId, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(postId)}/comments/${encodeURIComponent(commentId)}`, {
      method: 'DELETE', csrfToken,
    }),
  editReply: (postId, commentId, replyId, reply, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(postId)}/comments/${encodeURIComponent(commentId)}/replies/${encodeURIComponent(replyId)}`, {
      method: 'PUT', body: { reply }, csrfToken,
    }),
  deleteReply: (postId, commentId, replyId, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(postId)}/comments/${encodeURIComponent(commentId)}/replies/${encodeURIComponent(replyId)}`, {
      method: 'DELETE', csrfToken,
    }),
  setCommentPinned: (postId, commentId, pinned, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(postId)}/comments/${encodeURIComponent(commentId)}/pin`, {
      method: 'PUT', body: { pinned }, csrfToken,
    }),
  notifications: () => apiRequest('/api/notifications'),
  markNotificationsRead: (csrfToken) =>
    apiRequest('/api/notifications/read', { method: 'PUT', csrfToken }),
  markNotificationRead: (id, csrfToken) =>
    apiRequest(`/api/notifications/${encodeURIComponent(id)}`, { method: 'PUT', csrfToken }),
  deleteNotification: (id, csrfToken) =>
    apiRequest(`/api/notifications/${encodeURIComponent(id)}`, { method: 'DELETE', csrfToken }),
  notificationPreferences: () => apiRequest('/api/notification-preferences'),
  updateNotificationPreferences: (preferences, csrfToken) =>
    apiRequest('/api/notification-preferences', { method: 'PUT', body: preferences, csrfToken }),
  privacy: () => apiRequest('/api/me/privacy'),
  updatePrivacy: (settings, csrfToken) =>
    apiRequest('/api/me/privacy', { method: 'PUT', body: settings, csrfToken }),
  followRequests: () => apiRequest('/api/me/follow-requests'),
  respondFollowRequest: (userId, accept, csrfToken) =>
    apiRequest(`/api/me/follow-requests/${encodeURIComponent(userId)}`, {
      method: 'PUT', body: { accept }, csrfToken,
    }),
  feedback: (id, type, enabled, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(id)}/feedback`, {
      method: 'PUT', body: { type, enabled }, csrfToken,
    }),
  analytics: () => apiRequest('/api/me/analytics'),
  sessions: () => apiRequest('/api/me/sessions'),
  revokeOtherSessions: (csrfToken) => apiRequest('/api/me/sessions', { method: 'DELETE', csrfToken }),
  securityEvents: () => apiRequest('/api/me/security-events'),
  exportAccount: () => apiRequest('/api/me/export'),
  deleteAccount: (confirmation, csrfToken) =>
    apiRequest('/api/me/account', { method: 'DELETE', body: { confirmation }, csrfToken }),
  reportContent: (type, id, reason, details, csrfToken) =>
    apiRequest(`/api/reports/${encodeURIComponent(type)}/${encodeURIComponent(id)}`, {
      method: 'POST', body: { reason, details }, csrfToken,
    }),
  savePushSubscription: (subscription, csrfToken) =>
    apiRequest('/api/push-subscriptions', { method: 'POST', body: subscription, csrfToken }),
  adminMetrics: () => apiRequest('/api/admin/metrics'),
  setSaved: (id, saved, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(id)}/save`, {
      method: 'PUT', body: { saved }, csrfToken,
    }),
  savedVideos: () => apiRequest('/api/me/saved'),
  history: () => apiRequest('/api/me/history'),
  recordView: (id, watchSeconds, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(id)}/view`, {
      method: 'POST', body: { watchSeconds }, csrfToken,
    }),
  recordShare: (id, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(id)}/share`, { method: 'POST', csrfToken }),
  adminStats: () => apiRequest('/api/admin/stats'),
  adminReports: (status = 'all') =>
    apiRequest(`/api/admin/reports?status=${encodeURIComponent(status)}`),
  updateReportStatus: (id, status, csrfToken) =>
    apiRequest(`/api/admin/reports/${encodeURIComponent(id)}`, {
      method: 'PUT', body: { status }, csrfToken,
    }),
  setSuspended: (id, suspended, csrfToken) =>
    apiRequest(`/api/admin/users/${encodeURIComponent(id)}/suspension`, {
      method: 'PUT', body: { suspended }, csrfToken,
    }),
  upload: (form, csrfToken, options) => uploadRequest('/api/videos', form, csrfToken, options),
  deletePost: (id, csrfToken) =>
    apiRequest(`/api/posts/${encodeURIComponent(id)}`, { method: 'DELETE', csrfToken }),
}

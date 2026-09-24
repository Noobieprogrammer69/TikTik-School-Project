# C++ API reference

The browser uses same-origin `http://localhost:3000/api/...`; Vite or Nginx proxies to Drogon. Direct backend examples use `http://localhost:8080`.

## Conventions and security

Success is `{ "data": ... }`. Errors are `{ "error": { "code": "...", "message": "..." } }` with an appropriate HTTP status. Common statuses are 400 validation, 401 authentication, 403 CSRF/origin/authorization, 404 missing, 409 conflict, 413 size, 415 media type, 416 range, 429 login rate limit, and 503 database unavailable.

Google is the only real provider because it is the provider in the supplied project. C++ verifies the ID token signature, key ID, audience, issuer, expiration, subject, and verified email. Login creates a random `tt_session` cookie (`HttpOnly`, `SameSite=Lax`, expiring); MongoDB stores only its SHA-256 hash. `GET /api/auth/session` returns an in-memory CSRF token. Every cookie-authenticated mutation requires that value in `X-CSRF-Token` and requires an exact `FRONTEND_ORIGIN` when the browser sends `Origin`. Acting user IDs always come from the session.

`/api/test/login` exists only with `ENABLE_TEST_AUTH=true`, requires `X-Test-Auth-Key`, and must never be enabled publicly.

## Health and authentication

| Method and path | Access | Request/result |
|---|---|---|
| `GET /api/health` | Public | Database health; used by Compose. |
| `GET /api/config/public` | Public | `{googleClientId,maxUploadBytes,mediaProcessing}`; never returns secrets. |
| `POST /api/auth/google` | Public/rate limited | `{credential}`; verifies Google token and creates session. |
| `GET /api/auth/session` | Cookie | Restores session, rotates CSRF value, returns `{user,csrfToken,expiresAt}`. |
| `POST /api/auth/logout` | Cookie + CSRF | Revokes the MongoDB session and expires cookie. |

## Users, profiles, search, and feeds

| Method and path | Access | Request/result |
|---|---|---|
| `GET /api/users` | Public | Up to 100 accounts. |
| `GET /api/profiles/:id` | Public/optional cookie | `{user,userVideos,userLikedVideos}` plus follow counts. Private videos require owner/approved-follower access; liked videos respect their visibility setting. |
| `GET /api/search?q=term` | Public | Matching users/videos; query is 1–100 bytes. |
| `GET /api/posts[?topic=Gaming]` | Public | Legacy-compatible newest-first list. |
| `GET /api/posts/:id` | Public | One post with author/media/social data. |
| `GET /api/feed?mode=for_you&limit=12&cursor=...` | Optional cookie | Cursor page `{items,nextCursor}` with recommendation reasons. |
| `GET /api/feed?mode=following` | Cookie | Only followed creators. |
| `GET /api/feed?hashtag=cpp&topic=Gaming` | Optional cookie | Normalized hashtag/topic filter. |
| `GET /api/me/profile` | Cookie | Current editable profile and `isAdmin`. |
| `PUT /api/me/profile` | Cookie + CSRF | `{userName,bio}`; name 2–50, bio at most 160 bytes. |
| `POST /api/me/avatar` | Cookie + CSRF | Multipart `avatar`: JPEG/PNG/WebP, at most 5 MiB. |
| `GET /api/avatars/:userId` | Public | Custom locally stored avatar. |
| `GET /api/me/privacy` | Cookie | Private-account, messages/comments audience, and liked-video visibility. |
| `PUT /api/me/privacy` | Cookie + CSRF | Partial `{privateAccount,messagesFrom,commentsFrom,showLikedVideos}` update. Audience values are `everyone`, `followers`, or `none`. |
| `GET /api/me/analytics` | Cookie | Creator totals and per-video views/likes/comments/shares. |

User JSON deliberately keeps `_id`, `_type`, `userName`, and `image`; post JSON keeps `_id`, `caption`, `topic`, `userId`, `postedBy`, `likes`, `comments`, and `video.asset.url` so the converted UI remains understandable.

## Relationships, safety, and reports

| Method and path | Access | Request/result |
|---|---|---|
| `GET /api/users/:id/relationship` | Cookie | `{following,requested,followerCount,followingCount}`. |
| `PUT /api/users/:id/follow` | Cookie + CSRF | `{follow}`; follows public accounts or creates/cancels a private-account request; idempotent; self-follow rejected. |
| `GET /api/me/follow-requests` | Cookie | Pending requests with requester profile data. |
| `PUT /api/me/follow-requests/:requesterId` | Cookie + CSRF | `{accept}` accepts or declines the owned pending request. |
| `POST /api/users/:id/report` | Cookie + CSRF | `{reason,details}`; one rerunnable private report per pair. |
| `GET /api/users/:id/safety` | Cookie | `{blocked,blockedBy,muted}`. |
| `PUT /api/users/:id/block` | Cookie + CSRF | `{blocked}`; removes follows and prevents chat/social interaction. |
| `PUT /api/users/:id/mute` | Cookie + CSRF | `{muted}`; hides creator from that user's feeds. |
| `GET /api/users/:id/presence` | Cookie | `{online}` unless either user blocked the other. |

Report reasons: `spam`, `harassment`, `impersonation`, `inappropriate`, `other` (details at most 500 bytes). Reports never notify the target.

`POST /api/reports/:type/:id` reports a `video`, participating user's `message`, or `comment` using the same reason/details validation. It is cookie + CSRF protected and reruns update the existing reporter/target report.

## Upload, playback, and video actions

`POST /api/videos` uses cookie + CSRF multipart data:

- `video`: one MP4, WebM, Ogg, or OGV file;
- `caption`: 1–150 bytes; hashtags are normalized and stored;
- `topic`: one configured topic;
- optional `subtitles`: UTF-8 WebVTT `.vtt`/`text/vtt`;
- optional `cover`: JPEG/PNG/WebP, at most 5 MiB.

C++ enforces `MAX_UPLOAD_BYTES`, signature/MIME/extension agreement, random safe names, and session ownership. When enabled, FFmpeg creates 360p and 720p MP4 variants and a JPEG thumbnail unless a validated custom cover is supplied. If processing fails, the validated original remains usable. Large bytes stay in `MEDIA_ROOT`; MongoDB stores metadata/references only.

| Method and path | Access | Request/result |
|---|---|---|
| `GET /api/media/:mediaId[?quality=360|720]` | Public/optional cookie | Authorized video/thumbnail/VTT with correct type; video supports single HTTP byte ranges (`200`, `206`, `416`). Private-account video access requires owner/approved follower. |
| `PUT /api/posts/:id/like` | Cookie + CSRF | `{like}`; `$addToSet`/`$pull` keeps it idempotent. |
| `DELETE /api/posts/:id` | Cookie + CSRF | Owner-only; removes post, video, thumbnail, and subtitles. |
| `PUT /api/posts/:id/save` | Cookie + CSRF | `{saved}`; unique private save. |
| `GET /api/me/saved` | Cookie | Saved Library items. |
| `POST /api/posts/:id/view` | Public; cookie + CSRF when signed in | `{watchSeconds}`; every explicit open/play/loop event increments the view count. Signed-in history remains one latest entry per user/video; anonymous views create no private history. |
| `GET /api/me/history` | Cookie | Latest view history. |
| `POST /api/posts/:id/share` | Cookie + CSRF | Records the event, increments `shareCount`, and notifies the video owner when another user shares it. |
| `PUT /api/posts/:id/feedback` | Cookie + CSRF | `{type:"not_interested"|"hide_creator",enabled}` affects only that user's feed and is reversible. |

## Comments and replies

All mutations use cookie + CSRF and derive the actor from the session.

| Method and path | Body/authorization |
|---|---|
| `POST /api/posts/:postId/comments` | `{comment}`; 1–500 bytes. |
| `PUT /api/posts/:postId/comments/:commentId/like` | `{like}`; idempotent. |
| `POST /api/posts/:postId/comments/:commentId/replies` | `{reply}`; 1–500 bytes. |
| `PUT /api/posts/:postId/comments/:commentId/pin` | `{pinned}`; video owner only, one pin. |
| `PUT /api/posts/:postId/comments/:commentId` | `{comment}`; comment author only; adds `editedAt`. |
| `DELETE /api/posts/:postId/comments/:commentId` | Author or video owner. |
| `PUT /api/posts/:postId/comments/:commentId/replies/:replyId` | `{reply}`; reply author only. |
| `DELETE /api/posts/:postId/comments/:commentId/replies/:replyId` | Reply author or video owner. |

## Notifications

Types: `follow`, `follow_request`, `follow_accepted`, `new_post`, `post_like`, `post_share`, `comment`, `reply`, `mention`, `comment_like`, `comment_pin`, `message`. Self-actions do not notify. Undoing follow/like/pin removes its state notification.

| Method and path | Access | Result |
|---|---|---|
| `GET /api/notifications` | Cookie | Latest 100 with actor, targets, date, read state. |
| `PUT /api/notifications/read` | Cookie + CSRF | Mark all read. |
| `PUT /api/notifications/:id` | Cookie + CSRF | Mark one owned notification read. |
| `DELETE /api/notifications/:id` | Cookie + CSRF | Delete one owned notification. |
| `GET /api/notification-preferences` | Cookie | Booleans for every category. |
| `PUT /api/notification-preferences` | Cookie + CSRF | Updates category booleans. |

## Private chat and realtime

| Method and path | Access | Result |
|---|---|---|
| `GET /api/chat/conversations` | Cookie | Other user, last message, unread count, mute/archive state. |
| `GET /api/chat/:userId/messages` | Cookie | Latest 100 messages oldest-to-newest. |
| `POST /api/chat/:userId/messages` | Cookie + CSRF | `{message,replyTo?}`; 1–1,000 text bytes. |
| `PUT /api/chat/:userId/read` | Cookie + CSRF | Marks inbound messages/read notifications. |
| `POST /api/chat/:userId/attachments` | Cookie + CSRF | One JPEG/PNG/WebP up to 10 MiB, optional text/reply. |
| `GET /api/chat/media/:mediaId` | Cookie | Attachment only if current user is a participant. |
| `PUT /api/chat/messages/:id/reaction` | Cookie + CSRF | `{emoji}`; one per user; empty removes. |
| `DELETE /api/chat/messages/:id` | Cookie + CSRF | Sender-only soft delete; removes attachment file. |
| `PUT /api/chat/:userId/settings` | Cookie + CSRF | `{muted,archived}` private per user. |
| `GET /api/chat/:userId/history?before=id&limit=50` | Cookie | Cursor page `{items,nextCursor}` for older messages. |
| `GET /api/chat/:userId/search?q=term&limit=50` | Cookie | Searches non-deleted conversation text; term is 2–100 characters. |
| `PUT /api/chat/messages/:id/edit` | Cookie + CSRF | `{message}`; sender-only text edit and realtime `message_updated` event. |

Connect to `ws://localhost:3000/api/realtime`; the `tt_session` cookie authenticates the upgrade. Clients send `{type:"typing",userId,typing}` or `{type:"presence_request",userId}`. Server events are `ready`, `message`, `message_updated`, `notification`, `typing`, and `presence`. They are fast UI hints; MongoDB-backed HTTP reads remain the source of truth after reconnect.

## Account, devices, and data

| Method and path | Access | Result |
|---|---|---|
| `GET /api/me/sessions` | Cookie | Active server-side sessions, creation/expiry time, and which one is current. |
| `DELETE /api/me/sessions` | Cookie + CSRF | Revokes all sessions except the current one. |
| `GET /api/me/security-events?limit=30` | Cookie | Recent login/logout/settings/session security events. |
| `GET /api/me/export` | Cookie | Downloadable JSON profile, privacy, owned posts, saved/history, notifications, and session metadata. |
| `DELETE /api/me/account` | Cookie + CSRF | `{confirmation:"DELETE"}` permanently removes the account, related records, media, and current cookie. |
| `POST /api/push-subscriptions` | Cookie + CSRF | Stores `{endpoint,keys:{p256dh,auth}}` for future Web Push delivery. |
| `DELETE /api/push-subscriptions` | Cookie + CSRF | Deletes the current user's supplied subscription endpoint. |

## Administration

Only IDs in comma-separated `ADMIN_USER_IDS` may access these routes. All mutations require CSRF.

| Method and path | Result |
|---|---|
| `GET /api/admin/stats` | Counts for users, videos, messages, follows, open reports, views, saves, and topic breakdown. |
| `GET /api/admin/metrics` | Same application counts plus backend uptime, database state, and media-directory availability. |
| `GET /api/admin/reports?status=open` | Queue filtered by `all/open/reviewed/resolved/dismissed`. |
| `PUT /api/admin/reports/:id` | `{status}`; records moderator and timestamp. |
| `PUT /api/admin/users/:id/suspension` | `{suspended}`; suspension revokes all target sessions. |

## CORS and HTTPS

Only exact `FRONTEND_ORIGIN` receives credentialed CORS headers. Docker is same-origin through Nginx. For a real HTTPS host set `FRONTEND_ORIGIN=https://your-host` and `COOKIE_SECURE=true`. Keep `COOKIE_SECURE=false` for local HTTP or the browser will refuse the cookie.

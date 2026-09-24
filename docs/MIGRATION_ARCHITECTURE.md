# Migration architecture and plan

## Why this shape

The migrated project uses one C++ application backend, one browser frontend, and MongoDB:

```text
Browser :3000
  -> Vite dev proxy or Nginx production proxy (/api/*)
      -> Drogon C++20 API :8080
          -> MongoDB (documents, relationships, sessions, activity)
          -> persistent media directory (videos, posters, captions, avatars, chat images)
          -> authenticated WebSocket connections (chat, typing, presence, notification hints)
          -> Google public signing keys (only during Google login/key refresh)
```

The old pages used Next server-side rendering, but the shell deliberately returned `null` until browser hydration, and all useful data was fetched through internal HTTP calls. A client-rendered Vite application therefore preserves the observed behavior while removing the unnecessary JavaScript application server. React Router preserves the original routes and adds `/messages`, `/messages/:userId`, `/notifications`, `/library`, `/settings`, and `/admin`; Vite and Nginx both fall back to `index.html` for direct navigation.

Node.js remains a frontend build tool only. All application authentication, authorization, data access, upload handling, and business mutations move to C++.

## Backend design

- C++20 and Drogon 1.9.13.
- Official MongoDB C++ driver 4.4.0, accessed through `mongocxx::pool` so each request checks out a safe client connection.
- jwt-cpp 0.7.2/OpenSSL for local Google OpenID Connect ID-token verification; Google `tokeninfo` is not used because Google documents it as a debugging endpoint.
- Catch2 3.16.0 for focused C++ tests.
- CMake plus vcpkg manifest mode pinned to vcpkg commit `a1cae005c39be7b18ba319fced856b68d7276271`.

The implementation was cross-checked against the upstream API documentation: MongoDB's [C++ connection-pool guide](https://www.mongodb.com/docs/languages/cpp/cpp-driver/current/connect/connection-pools/) confirms one acquired client per request/thread; Drogon's [file-response API](https://github.com/drogonframework/drogon/blob/master/lib/inc/drogon/HttpResponse.h) documents ranged file responses; [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) documents issuer/audience verification; and Google's [server-side ID-token guide](https://developers.google.com/identity/gsi/web/guides/verify-google-id-token) requires signature, audience, issuer, expiry, and stable `sub` checks. The pinned MongoDB 4.4 family remains listed by MongoDB as a stable driver family and compiled against the exact APIs used here.

Public reads remain available without login. Mutations require an opaque, random `HttpOnly` session cookie and a matching CSRF header. The session token is never stored in plaintext in MongoDB; only its SHA-256 hash is stored. Session expiry uses both an application check and a MongoDB TTL index. Logout revokes/deletes the server session before expiring the cookie. Login attempts are rate-limited by source address.

The acting user is always derived from the verified session. Browser-supplied user IDs are not accepted for likes, comments, uploads, deletes, or message senders.

## MongoDB collections

- `users`: stable Google-subject `_id`, editable display name/bio/avatar metadata, notification preferences, moderation state, provider metadata, and timestamps.
- `posts`: stable string `_id`, caption/topic/hashtags, owner ID, video/poster/subtitle metadata, unique like-user array, embedded comments/replies, view/share counters, timestamps.
- `sessions`: SHA-256 token `_id`, user ID, SHA-256 CSRF secret, created/expiry timestamps, and revocation metadata.
- `messages`: random `_id`, canonical conversation ID, participant/sender/recipient IDs, text, reply link, reactions, optional private attachment, read/deleted timestamps.
- `follows`: deterministic relationship ID, follower/following IDs, and creation timestamp.
- `reports`: reporter/target IDs, validated reason/details, review status, and timestamps. A unique reporter-target index prevents duplicates.
- `notifications`: deterministic or event ID, recipient, actor, event type, optional post/comment target, creation timestamp, and optional read timestamp.
- `blocks`, `mutes`: directional account-safety relationships.
- `saves`, `view_history`, `share_events`: private library/activity and aggregate inputs.
- `conversation_settings`: per-user mute/archive state for each one-to-one conversation.
- `follow_requests`: pending requester/target records for private accounts.
- `feed_feedback`: per-user not-interested and hidden-creator signals.
- `security_events`: account login/logout/settings/session audit entries.
- `push_subscriptions`: per-user browser endpoints and Web Push public keys; secrets remain server-side.

Indexes enforce Google-subject uniqueness, post owner/topic/hashtag/feed lookup, automatic session expiry, message/conversation/attachment lookup, follower/request relationships, deduplicated reports/blocks/mutes/saves/views/settings/feedback/subscriptions, security-event lookup, moderation queues, and notification inbox/unread lookup.

Large media bytes live under configurable `MEDIA_ROOT`; MongoDB stores only metadata and generated storage names. The media route implements byte ranges for playback/seeking. FFmpeg is optional in native development and included in Docker; it normalizes non-MP4 uploads and creates posters. User-supplied WebVTT stays separate from video bytes.

## Routine decisions

- Vite replaces Next SSR/API routes to remove the JavaScript server from production and avoid retaining backend responsibilities in Node.
- Google remains the only login method because it is the only provider present. Password credentials are not invented.
- A user's first verified Google login creates their clean MongoDB account using Google's stable `sub` claim.
- Private one-to-one chat and persistent dark mode were added later at the project owner's request. Drogon's in-process WebSocket controller provides instant messages, edits, typing, presence, and notification hints; HTTP polling remains a simple recovery fallback. Cursor history, conversation search, date separators, read state, reporting, and scroll-position preservation stay within the same C++ service and require no broker.
- Follow/unfollow, private reports, comment likes/replies/pinning, and activity notifications were added later at the project owner's request. Reversible actions use deterministic notification IDs so unlike, unfollow, and unpin can remove the matching notification without affecting unrelated history. Reports stay private and never notify the reported user. Owner-only post deletion is included because local media needs a safe lifecycle and the requested verification explicitly covers deletion/authorization.
- The default upload limit is configurable and the UI reads the configured public limit. Drogon keeps at most 16 MiB of a request body in RAM before using its temporary-file path, while the final media remains in the persistent directory. Accepted formats remain MP4, WebM, and Ogg; the server checks MIME type, extension, and file signature.
- Historical application source and cloud-import tooling are intentionally not retained; the running project starts from clean MongoDB collections.
- The recommendation system is intentionally explainable rather than machine-learned: follows and recently watched topics receive score boosts, likes contribute popularity, and the UI displays the reason.
- Blocking is stronger than muting. A block removes follows and cross-user notifications and denies chat/social actions in either direction. A mute only hides that creator's posts for the acting user.
- `ADMIN_USER_IDS` is a server-only allowlist. Suspension revokes all sessions immediately; administrators cannot suspend themselves.
- Privacy is enforced at the data/API layer, not only hidden in React. Private videos and their media require owner/approved-follower access; message/comment audience rules are checked before writes; liked-video visibility is respected on profiles.
- Upload drafts live only in the current browser's IndexedDB. Upload progress/cancellation uses `XMLHttpRequest` because Fetch does not expose portable request upload progress. Uploaded media still goes directly to the protected C++ endpoint.
- Video qualities are ordinary 360p/720p MP4 variants chosen by the browser, so range seeking stays simple and demonstrable. This is deliberately not an HLS/DASH service.
- View history and view counting are intentionally separate: MongoDB keeps one latest history row per signed-in account/video for the Library, while every explicit video open/feed play and each completed loop atomically increments the video's public counter. Anonymous events count but create no private history row.
- The service worker provides an installable shell and background-tab system notifications. Full remote delivery after every tab is closed needs deployment-specific VAPID keys and a Web Push sender; the backend already persists subscriptions but does not pretend this external step is complete.
- Account export is JSON for transparency. Permanent deletion removes the user's MongoDB records and locally owned media, expires the session cookie, and requires an exact `DELETE` confirmation.

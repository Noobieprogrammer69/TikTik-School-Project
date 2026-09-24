# Feature migration checklist

This checklist treats the supplied application—not the real TikTok product—as the scope. “Verification” entries describe the concrete automated or manual check used after implementation.

Implementation is complete for every replacement row below. Linux unit, MongoDB, HTTP, production frontend, container, Google-login, and browser checks passed. Native Windows/MSVC remains unverified because no Windows execution environment was available. See `VERIFICATION.md`.

| Feature | Original implementation | Replacement implementation | Verification |
|---|---|---|---|
| App shell/design | Next `_app.tsx`, Navbar, Sidebar, Tailwind | Vite React shell using converted JSX, responsive card layout, accessible controls, and a cohesive visual system | Production frontend build and Playwright responsive smoke test |
| Light/dark appearance | Not present | System-aware theme with a navigation toggle, persistent browser preference, dark-compatible existing screens, and no flash on startup | Theme component test, production build, and Playwright toggle/reload check |
| Routes/direct navigation | Next Pages Router routes | React Router routes with Vite dev fallback and Nginx `try_files` fallback | Passed Playwright production direct routes through the running Nginx container |
| Google login | Browser-decoded Google credential; Zustand local persistence | Google credential sent to C++; C++ verifies RS256 signature/issuer/audience/expiry using Google's published PEM keys, then creates an opaque MongoDB-backed session | Real provider login confirmed; current-key recognition and invalid-signature rejection verified |
| Session restoration | Persisted unverified user object in local storage | `HttpOnly` expiring session cookie plus `GET /api/auth/session`; CSRF token returned to browser memory | Integration refresh/expiration/protected-route tests |
| Logout | `googleLogout()` plus local state removal | Google client cleanup plus server-side session revocation and expired cookie | Integration logout/reuse test and Playwright logout flow |
| Main feed | SSR `GET /api/post`, GROQ newest-first | Browser fetch of `GET /api/posts`; MongoDB sort by `createdAt` descending | Store/API integration test and Playwright feed check |
| Topic filtering | `/?topic=...` plus `/api/discover/[topic]` | Same query-string UI; `GET /api/posts?topic=...` with validated MongoDB filter | Passed API topic feed and validation tests; links preserved in rendered UI |
| Suggested accounts | `GET /api/users`, first six | `GET /api/users`, same first-six presentation | API test and rendered account links |
| Search | SSR video search plus client-side account filtering | `GET /api/search?q=...` returns matching videos/users; same two tabs | Passed API video/account search and Playwright search/empty tabs |
| Profiles | SSR user/posts/liked GROQ queries | `GET /api/profiles/:id` with MongoDB user, owned posts, liked posts | API test; direct profile route and tab checks |
| Upload preview | Direct Sanity upload and returned CDN URL | Browser object URL preview; one multipart upload to protected C++ endpoint | Frontend component/E2E flow |
| Persistent video upload | Browser writes file to Sanity, then creates post | C++ validates MIME/signature/size, creates safe random filename in persistent media directory, records metadata in MongoDB | Validation/API tests plus MongoDB record and range-readable media preserved across backend container recreation |
| Video playback/seeking | Sanity CDN URL in HTML video | C++ media endpoint with content type, `Accept-Ranges`, and validated single range responses | Range parser unit tests; HTTP `Range` integration test; Playwright seek check |
| Likes/unlikes | Sanity patch using browser user ID; duplicates possible | Protected C++ mutation derives user from session and uses MongoDB `$addToSet`/`$pull` | Idempotency, persistence, CSRF, and unauthenticated tests |
| Comments | Sanity patch using browser user ID | Protected C++ mutation derives author from session and appends validated embedded comment | Passed API persistence, validation, CSRF, unauthenticated, and browser tests |
| Private chat | Not present | Added one-to-one messages through protected C++ routes/WebSocket events, persistent MongoDB messages, conversation ordering, unread/read state, user discovery, responsive UI, and polling fallback | Validation/MongoDB/API tests plus two-user Playwright send/reply journey |
| Follow/unfollow | Not present | Added idempotent MongoDB follow records, private-account requests with accept/decline/cancel, public counts, profile controls, self-follow prevention, and request/accepted/follow notifications | MongoDB/API idempotency/private-request suite and two-user browser checks |
| Account reporting | Not present | Added a private report dialog, validated reasons/details, reporter-derived identity, and one rerunnable report per reporter/target | Validation, MongoDB deduplication, API, and browser checks |
| Comment replies | Not present | Added nested persistent replies with authenticated authors and reply notifications | MongoDB/API persistence and two-user browser checks |
| Comment likes | Not present | Added atomic like/unlike arrays, duplicate prevention, counts, controls, and reversible notifications | MongoDB/API idempotency and browser checks |
| Pinned comments | Not present | Added one pinned comment per video with owner-only authorization, highlighted UI, and reversible notification | Owner/non-owner MongoDB/API checks and browser check |
| Activity notifications | Not present | Added a persistent filtered/grouped center, live toast, badges, and preferences for follows/requests/acceptances, mentions, uploads, video likes/shares, comments, replies, comment likes, pins, and messages | MongoDB/API read-state/share/preference checks and two-user browser journey |
| Empty/loading/error states | Partial text/icon states; many rejected requests uncaught | Preserved empty/loading states plus visible retryable API errors | Component/E2E empty and error-state checks |
| Post deletion | Not present | Added as a small migration safety feature: owner-only delete removes MongoDB post and its local media | Owner/non-owner API tests and owner-only UI/E2E check |
| Editable profiles and avatars | Not present | Protected name/bio update plus validated JPEG/PNG/WebP avatar storage; later Google logins preserve custom values | MongoDB/API profile and avatar tests; Settings UI |
| For You and Following feeds | New upgrade | Cursor-paginated C++ feed endpoint; following-only filter and simple explainable ranking from follows, recent watch topics, likes, and recency | MongoDB/API feed tests; Home tabs and infinite-scroll browser check |
| Comment edit/delete/moderation | New upgrade | Authors edit/delete their own comments/replies; video owner can remove discussion on their video; edited timestamp retained | Store/API authorization and persistence tests |
| Block and mute | New upgrade | Idempotent safety records; blocks remove follows/activity and prevent feed/chat/social interaction; mute hides creators without notifying them | Store/API safety and feed-filter tests; profile/chat controls |
| Moderation dashboard | New upgrade | Admin allowlist, report queue/status workflow, account suspension with session revocation, and authorization checks | API admin/non-admin/suspension tests and `/admin` UI |
| Pagination and infinite scrolling | New upgrade | Bounded cursor pages with a browser `IntersectionObserver` sentinel | API page-shape checks and browser feed journey |
| Video thumbnails | New upgrade | C++ invokes configurable FFmpeg to create local JPEG posters after upload; failures fall back safely to video-only playback | Container FFmpeg packaging, upload response, and rendered `poster` checks |
| Hashtags | New upgrade | Server extracts normalized unique tags into MongoDB; hashtag links/filter route through the C++ feed API | Store/API hashtag tests and rendered links |
| Saved videos | New upgrade | Unique `(userId, postId)` records and private Library screen | Store/API idempotency and Library checks |
| Watch history and view counts | New upgrade | One durable history record per signed-in user/video, with an event-based public counter that increments for signed-in or anonymous video opens/plays and completed loops | Store/API repeated/anonymous-view tests and Library history tab |
| Sharing | New upgrade | Web Share API with clipboard fallback; C++ records share event/count and creates an owner notification | Store/API notification test and two-user browser check |
| Realtime chat transport | Polling chat was added during migration | Authenticated WebSocket hub publishes new messages/notifications; reconnect and polling fallback remain; scrolling preserves the reader's position and offers a new-message jump control | C++ build, API persistence, and two-context browser delivery/scroll check |
| Typing and presence | New upgrade | Ephemeral WebSocket typing events and authenticated online-state endpoint; nothing sensitive is persisted | Browser two-context chat check and protected endpoint check |
| Advanced messages | New upgrade | Reply references, per-user emoji reactions, sender deletion, and private validated image attachments | Store/API authorization, attachment access/deletion, reply, and reaction tests |
| Conversation controls | New upgrade | Per-user mute/archive state plus chat block/report shortcuts | Store/API persistence and Messages UI |
| Notification controls | Basic notification center was added during migration | Per-category preferences, mark-one/mark-all, delete-one, realtime badge refresh | Store/API preference/read/delete tests and Settings/Notifications UI |
| Recommendations | New upgrade | Understandable scoring favors followed creators and topics from recent viewing, with a displayed reason | Store/API ranking reason and Home rendering |
| Accessibility and captions | Partial labels/states | Keyboard buttons/labels, focus-visible styling, user-supplied WebVTT tracks, theme contrast, and reduced-motion CSS | Lint/build, upload checks, and manual/browser inspection |
| FFmpeg processing | New upgrade | Optional server-side MP4 normalization and JPEG thumbnail generation; configurable executable and safe original-file fallback | Final backend container build includes FFmpeg; valid WebM browser upload |
| Admin statistics | New upgrade | Counts for users, videos, messages, follows, open reports, views, saves, plus topic breakdown | Store/API admin authorization and stats tests; dashboard rendering |
| Privacy and private accounts | New upgrade | C++-enforced profile/media visibility, follow approval, messaging/comment audiences, and liked-video visibility | Next API private follow/message tests and MongoDB integration |
| Feed feedback | New upgrade | Per-user Not interested/Hide creator signals with optimistic removal and Undo | Store/API persistence and frontend build |
| Creator analytics | New upgrade | Protected totals and per-video views/likes/comments/shares in Settings | Next API test and Settings rendering |
| Chat history/search/edit | New upgrade | Cursor history with stable scrolling, text search, sender-only edits, realtime update event, date/read/edited UI | Next API history/search/edit tests and original long-chat browser journey |
| Upload drafts/progress/covers | New upgrade | IndexedDB drafts, byte progress, cancellation/retry, validated custom cover or captured frame | Media integration, upload browser journey, lint/build |
| Video variants | New upgrade | Optional FFmpeg 360p/720p MP4s, client Auto/manual selector, and ranged delivery | Focused media test for both qualities and `206` responses |
| Mentions and rich text | New upgrade | Linked hashtags/mentions and deduplicated mention notifications from captions/comments/replies | Backend notification logic and production UI build |
| Device and security management | New upgrade | Active sessions, revoke-other-devices, and login/logout/settings/session event log | Next API suite |
| Export and deletion | New upgrade | Downloadable account JSON and confirmation-gated deletion of records/media/session | Next API suite |
| PWA/desktop notifications | New upgrade | Manifest/service worker/install prompt and system notifications for open/background tabs; Web Push subscription persistence foundation | Production build; closed-browser remote delivery documented as unverified |
| Backup/restore | New upgrade | Bash and PowerShell MongoDB+media scripts; outputs are Git-ignored | Shell review/syntax; destructive restore not run against real data |

Still intentionally out of scope: password registration/login (the source project used Google only), public deployment infrastructure, group chat, livestreaming, a machine-learning recommendation service, and deployed VAPID delivery after every browser window is closed. These were not part of the supplied application, or require deployment credentials/infrastructure that were not supplied.

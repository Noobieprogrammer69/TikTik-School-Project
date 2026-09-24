# Twenty-upgrade implementation notes

This pass groups the requested usability and product improvements into twenty understandable, testable areas. It does not claim to reproduce every feature of the commercial TikTok service.

| # | Upgrade | What is implemented | Verification |
|---:|---|---|---|
| 1 | Private accounts | Profile/video/media visibility is checked in C++; owners approve follow requests. | MongoDB/API private-access and follow-request tests. |
| 2 | Audience controls | Messages and comments can allow everyone, approved followers, or nobody; liked-video visibility is configurable. | API privacy and denied-message tests. |
| 3 | Feed controls | Not interested and Hide creator remove an item immediately and support Undo. | Store/API feedback checks and frontend production build. |
| 4 | Better recommendations | Ranking uses follows, viewing topics, popularity, recency, mute/block, and user feedback with a visible reason. | MongoDB feed/ranking tests. |
| 5 | Creator analytics | Settings shows total videos/views/likes/comments/shares plus per-video results. | Protected analytics API test. |
| 6 | Upload progress | XMLHttpRequest reports byte progress and permits cancellation/retry. | Frontend lint/build and upload E2E. |
| 7 | Local upload drafts | Video, caption, category, captions, and cover are saved in browser IndexedDB and can be restored/discarded. | Unit/build validation and manual UI path. |
| 8 | Cover tools | Creator can upload a validated image or capture the current video frame. | FFmpeg/media integration test verifies a custom PNG cover. |
| 9 | Video quality choices | C++/FFmpeg creates 360p and 720p MP4s; Auto/manual selection preserves playback time and range seeking. | Media test verifies both variants and byte ranges. |
| 10 | Message history | Cursor pagination loads older messages while preserving the reader's scroll position. | Next API suite and original long-chat Playwright journey. |
| 11 | Conversation search/edit | Search non-deleted conversation text and edit only messages sent by the acting user; realtime clients receive edits. | Next API suite and C++ authorization logic. |
| 12 | Chat usability | Date separators, read/edited labels, image lightbox, reporting, jump-to-latest, responsive height, and reliable internal scrolling. | Production E2E long-chat/scroll checks and build. |
| 13 | Mentions | `@username` in posts/comments/replies links to search and creates a deduplicated notification. | C++ MongoDB logic and notification UI build. |
| 14 | Notification organization | All/Unread/Social/Videos filters, date groups, live toasts, follow-request/accepted/share/mention types, and category preferences. | API preference/share tests and two-user Playwright flow. |
| 15 | Device/session security | Settings lists active sessions, revokes every other session, and displays recent security activity. | Next API session/security-event suite. |
| 16 | Account data controls | Download JSON export or permanently delete the account and owned media after exact confirmation. | Next API export/deletion suite. |
| 17 | Content reporting | Accounts, videos, messages, and comments have server-validated, private, deduplicated reporting paths. | Original and next API report tests. |
| 18 | PWA and desktop experience | Manifest, service worker, install prompt, cached shell, and system notifications while open/backgrounded; subscription records prepare for a deployed Web Push sender. | Vite production build; closed-browser delivery is explicitly unverified. |
| 19 | Administration/health | Moderators see report workflow, usage counts, uptime, database state, and media-directory health. | Admin/non-admin API tests. |
| 20 | Backup and recovery | Cross-platform scripts back up/restore both MongoDB and persistent media, with ignored backup output. | Script syntax/review; restore is intentionally not run against real data. |

## Deliberate boundaries

- “Adaptive quality” here means choosing generated MP4 variants, not a production HLS/DASH transcoding cluster.
- The lightweight creator studio provides covers, captions, topics, draft, preview, progress, and quality generation. It is not a full non-linear video editor.
- Desktop notifications require an open or background application context. A production VAPID key pair and Web Push sender must be configured before claiming delivery when all browser windows are closed.
- Backups protect the school-demo deployment. The restore command replaces current MongoDB collections, so the team should create a fresh backup first.
- Google remains the only real login provider because that was the supplied project's actual provider.

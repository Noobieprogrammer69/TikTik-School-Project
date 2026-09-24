# School demonstration walkthrough

## Before class

1. Run `docker compose up -d` and wait for `docker compose ps` to show all three services healthy.
2. Open `http://localhost:3000` in a private test window.
3. Have one small MP4 or WebM and an optional `.vtt` caption file ready.
4. Confirm your Google test account is listed in the OAuth consent screen's test users.
5. Put the presenter's profile ID in `ADMIN_USER_IDS`, then restart the backend.

## Presentation script

1. **Architecture (30 seconds):** “React is now plain JavaScript built by Vite. Nginx serves it and proxies `/api` to one C++20 Drogon backend. The backend alone accesses MongoDB and the persistent media volume.”
2. **Login (30 seconds):** Log in with Google. Explain that the browser credential is verified by C++ with Google's public signing certificate; MongoDB holds only a hashed opaque session token, while the browser gets an `HttpOnly` cookie.
3. **Profile (30 seconds):** Open Settings, edit the display name/bio, upload an avatar, and refresh. Explain that custom values remain even after another Google login.
4. **Upload (60 seconds):** Select the clip and optional captions, enter a caption containing `#school` and an `@username`, choose a topic, capture or upload a cover, save/restore a draft, and post. Point out progress/cancel. Explain signature/size/type validation, FFmpeg 360p/720p variants, and that large bytes live in a media volume—not MongoDB.
5. **Playback and library (45 seconds):** Note the view counter, open/play the video and let it loop to show the counter increase for both events. Drag the seek control, switch Auto/360p/720p, save, and share. Mention `206 Partial Content`, then show that History still keeps one latest entry per video.
6. **Feeds (30 seconds):** Show For You/Following, a recommendation reason, a hashtag filter, and infinite loading.
7. **Private account (45 seconds):** Turn on Private account in Settings. From the second window, request to follow; accept it in Follow requests and show request/accepted notifications. Explain that C++ protects both profile data and media bytes.
8. **Comments and authorization (60 seconds):** From the second account, comment and mention the presenter. As the video owner, like the comment, reply, and pin it. Show notifications. Explain that only the video owner may pin and MongoDB `$addToSet` prevents duplicate likes.
9. **Private report (20 seconds):** Report a video or message, select a reason, and submit. Explain deduplication and why reports never notify the reported person.
10. **Private chat (60 seconds):** Click **Message**, show online/typing state, send enough messages to demonstrate scrolling, load older history, search, edit a sent message, and open an image attachment. Then reply/react in the other window. Explain authenticated WebSockets and MongoDB persistence.
11. **Dark mode (20 seconds):** Use the moon/sun button, hover a suggested-account card, refresh, and show that the dark appearance and readable hover colors remain active.
12. **Discovery and control (30 seconds):** Search the caption, show search history/suggestions, then use **Not interested** and Undo on a feed card.
13. **Account tools (30 seconds):** Show creator analytics, active sessions, security activity, data export, desktop notification permission, and the PWA install option in Settings. Do not actually delete the demo account.
14. **Safety and moderation (45 seconds):** Show mute/block/report, then open Moderation to resolve the report and show counts, uptime, database, and media health. Explain allowlisted admins and immediate session revocation on suspension.
15. **Authorization, backup, and persistence (30 seconds):** Show the owner-only Delete control. Explain that another account receives `403`, named volumes survive service restarts, and `scripts/backup.sh` captures both MongoDB and media.

## Useful questions to answer

- **Why not rewrite React in C++?** The requirement permits Node for frontend tooling; UI code remains browser JavaScript while all trusted backend work is C++.
- **Why no password registration?** The supplied project used Google only. The migration preserves that provider and does not invent passwords that cannot be safely associated with existing accounts.
- **Which actions notify users?** Follows/requests/acceptances, mentions, new uploads by followed creators, video shares/likes, comments, replies, comment likes, pins, and private messages. Each category can be disabled; self-actions do not notify and reports are private.
- **Will notifications appear after the browser is completely closed?** Not yet. Open/background tabs can show system notifications and the backend can store push subscriptions. A deployed VAPID/web-push sender is still required for closed-browser delivery.
- **Is the recommendation system AI?** No. It is a transparent school-project ranking using follows, recent watch topics, likes, and recency. The interface explains each recommendation.
- **Why MongoDB?** Its document and embedded-array model fits posts, likes, and comments, while indexes and atomic operators keep concurrent actions consistent.
- **Can Windows run it?** Yes: Docker Desktop runs the identical containers, and MSVC has its own CMake preset/debugger/CI job. Native Windows must still be tested on the groupmate's actual Windows machine.

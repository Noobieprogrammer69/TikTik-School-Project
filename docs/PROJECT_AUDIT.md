# Project audit (before migration)

Audit date: 2026-09-14 on Omarchy 4.0.1 (Arch Linux), x86-64.

## Repository state

- The supplied folder contains a `.git` directory, but it is empty/read-only and `git status` reports “not a git repository”. There is no commit history or branch to preserve and no migration branch can be created.
- No `AGENTS.md` or other project-specific contributor instructions were present.
- The source files date from March 2023. No automated tests or CI configuration were present.
- Existing environment variable names were `NEXT_PUBLIC_SANITY_TOKEN`, `NEXT_PUBLIC_GOOGLE_API_TOKEN`, and `NEXT_PUBLIC_BASE_URL`. Their values were not copied into documentation or new source.

## Existing architecture

- Frontend and JavaScript server: Next.js Pages Router (`next` 12 declaration with a lockfile resolving later 13-era tooling), React 18, TypeScript/TSX, Tailwind CSS, Axios, Zustand, and React Icons.
- User routes: `/`, `/upload`, `/detail/[id]`, `/profile/[id]`, and `/search/[searchTerm]`.
- Rendering: the feed, detail, profile, and search pages use `getServerSideProps`; the entire app deliberately renders nothing until the first browser effect has run, largely defeating SSR for the visible shell.
- Backend-for-frontend: Next API routes under `/api`. They contain the application's mutations and issue GROQ queries directly to Sanity.
- Data/content backend: Sanity project `cb90n0pe`, dataset `production`, with a separate Sanity Studio in `backend/`.
- Authentication: Google Identity Services via `@react-oauth/google`. The browser decodes the Google credential, trusts its claims, saves the user object in persistent Zustand/local storage, and POSTs it to Sanity through `/api/auth`. There is no server token verification, server session, expiration, logout invalidation, CSRF defense, authorization, or login rate limit.

## Existing API routes and business logic

| Existing route | Behavior before migration |
|---|---|
| `POST /api/auth` | Trust browser-supplied Google claims and create a Sanity user if absent. |
| `GET /api/users` | Return every Sanity user. |
| `GET /api/post` | Return every post ordered by `_createdAt` descending. |
| `POST /api/post` | Trust a browser-supplied document and create it in Sanity. |
| `GET /api/post/[id]` | Return one post with referenced author/media. |
| `PUT /api/post/[id]` | Append a comment using a browser-supplied user ID. |
| `PUT /api/like` | Append or remove a user reference using a browser-supplied user ID. Duplicate likes are possible. |
| `GET /api/profile/[id]` | Return a user, their posts, and posts whose likes contain their ID. |
| `GET /api/search/[searchTerm]` | Prefix-match post caption or topic. |
| `GET /api/discover/[topic]` | Prefix-match topic. |

Unsupported HTTP methods generally never receive a response, and errors are not consistently caught or translated.

## Sanity model and media usage

- `user`: `userName`, `image`; Google `sub` was used directly as `_id`.
- `post`: `caption`, file-valued `video`, duplicated string `userId`, `postedBy` user reference, user-reference array `likes`, inline `comments`, and string `topic`.
- `comment`: `postedBy` reference and string `comment`; comments are embedded into posts by the app even though a document schema also exists.
- Queries dereference authors and video assets, sort the main and profile feeds newest-first, search caption/topic, and locate liked posts by user reference.
- Video files are uploaded directly from the browser to Sanity using a public client token. Playback uses the Sanity CDN URL.
- The Sanity Studio is the only content-management/admin workflow.

## Implemented user-facing features

- Google login button and client-persisted identity; local logout.
- Main newest-first video feed and topic-filtered feed.
- Suggested accounts list.
- Video/account search with separate tabs.
- Public profile page with posted and liked video tabs.
- Video upload for MP4, WebM, or Ogg with caption and a fixed topic list.
- Feed/detail video playback, pause/play, mute/unmute, looping, native seeking controls on upload preview.
- Like/unlike on the detail page.
- Add/list comments on the detail page.
- Loading text during upload/comment actions and empty states for videos/comments.
- Responsive sidebar, navbar, search, profile, feed, detail, and upload layouts.

Not implemented in the supplied source: password registration/login, profile editing, follow/unfollow, replies, saved videos, sharing, notifications, pagination/infinite scrolling, moderation UI, post editing, or post deletion.

## Pre-existing failures and risks

- `npm ci`: passed after network access was allowed, but reported 22 dependency vulnerabilities (8 moderate, 12 high, 2 critical).
- `npm run lint`: passed with no warnings or errors.
- `npm run build`: frontend compilation passed, then the build failed while collecting page data. Next's bundled `jsonwebtoken` crashes on Node 26.7.0 with `TypeError: Cannot read properties of undefined (reading 'prototype')`, followed by `Call retries were exceeded`.
- The repository intentionally disables TypeScript build errors in `next.config.js`.
- Mutating APIs have no authentication or ownership enforcement and trust `userId` from the browser.
- Google credentials are decoded but not verified on the server.
- “Logout” only clears browser state; no server session exists to revoke.
- Like insertion is not idempotent and can create duplicates.
- Search terms and IDs are interpolated directly into GROQ strings.
- Uploads expose a Sanity write token to browser code; file limits and content are not enforced on the server.
- There is no deletion path for orphaned or unwanted media.
- Existing async API handlers lack consistent error handling and method responses.

## Available local toolchain

- Installed: GCC 16.2.1, Clang 22.1.8, Node 26.7.0, npm 11.19.0, VS Code 1.135.0, Docker 29.7.2, Docker Compose 5.5.0, and pkg-config 3.0.5.
- Missing at audit time: CMake, Ninja, vcpkg, mongosh, and Podman.
- Omarchy uses Arch packages. Native package instructions must use `omarchy pkg add ...`/Pacman conventions, not Ubuntu `apt` commands.


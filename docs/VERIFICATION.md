# Verification report

Run date: 2026-09-14 through 2026-09-24, Omarchy 4.0.1 / Arch Linux x86-64.

## Actually executed and passed

- Original baseline `npm ci`: passed with 22 reported vulnerabilities.
- Original baseline ESLint: passed.
- Original Next build: failed during page-data collection on Node 26 because its bundled `jsonwebtoken` accessed an invalid prototype. This is recorded as pre-existing in `PROJECT_AUDIT.md`.
- Migrated `npm install`: completed with 0 reported vulnerabilities.
- Migrated `npm run lint`: passed.
- Migrated `npm test`: 5 files, 10 tests passed, including profile-image fallback, API request behavior, chat request security, and theme persistence.
- Final frontend check `npm run lint && npm run build && npm test`: passed after the twenty-upgrade integration (77 modules; approximately 395.79 kB JavaScript; 5 files and 10 tests passed).
- Frontend container build with pinned Node 24.15.0: `npm ci` reported 0 vulnerabilities and the Vite production build passed.
- Pinned vcpkg resolution/build: Drogon 1.9.13, mongo-cxx-driver 4.4.0, jwt-cpp 0.7.2, Catch2 3.16.0, OpenSSL 3.6.4 and transitive dependencies built successfully.
- Native GCC 16 CMake Debug and Release builds: backend, unit tests, and MongoDB integration test executables compiled and linked.
- Backend container build: the multi-stage C++ build compiled Drogon, the official MongoDB C++ driver, backend, and both test executables; all 9 non-database C++ tests ran inside the build and passed before the FFmpeg runtime image was produced. Dependencies now occupy a source-independent cached layer.
- `ctest`: 9 unit cases passed in both Debug and Release. On this Omarchy desktop, `XDG_RUNTIME_DIR=/tmp` was used because the normal `/run/user/1000` temporary path was unavailable to Catch2 in the execution sandbox.
- Direct unit runner: 53 assertions in 9 cases passed, including private-message and report validation.
- Google signing-key compatibility: the verifier now consumes Google's official PEM `kid` map, with regression coverage for the live response shape. A probe using a current Google key ID and an intentionally fake signature reached cryptographic verification and was rejected as `failed to verify signature`, confirming that key lookup no longer fails as `unknown signing key`.
- Isolated MongoDB 8.0.30 integration runner: **96 assertions in 1 test case passed** on September 24. In addition to the original persistence/auth/social cases, it covers editable profiles, privacy/private-content access, follow requests, hashtags, Following/For You feeds, save/share idempotency, repeated event-based view counting with one history row, owner notifications, comment/reply authorization, block/mute behavior, reactions/message deletion, conversation settings, notification preferences, report review, suspension, and statistics.
- Original HTTP API integration passed against the isolated Compose C++ backend. The additional `npm run test:api-next` suite also passed privacy/private follow requests, message privacy, message edit/search/history, content reporting, creator analytics, device sessions, security events, export, and account deletion.
- Focused `npm run test:media` passed with FFmpeg enabled and a real WebM input: validated custom PNG cover, generated qualities exactly `360` and `720`, verified a `206`/100-byte MP4 range from both variants, verified cover `image/png`, then deleted and cleaned the post media.
- Playwright against an isolated production Compose frontend/backend with system Chromium on Omarchy: **2 tests passed in 27.1 seconds**. It covered two independent users, animated profile saving, profile name/bio/avatar, captioned real WebM upload, playback/seek, save/history/share, share notification/live toast, follow/report/notifications, comment edit/like/reply/pin, authenticated realtime typing/message/reply/reaction/image attachment, long-chat scrolling, scroll-position preservation, the new-message jump control, conversation mute, dark-mode hover/reload, search/profile, direct routes, owner deletion, logout, useful empty states, and responsive navigation.
- Persistent media and MongoDB survival across backend recreation were verified before the project was intentionally reset to a clean database.
- Docker Compose runtime: the final backend and frontend images rebuilt successfully from the repository; MongoDB 8.0.30, Drogon backend, and Nginx/React frontend all reached `healthy`; `/api` was reachable both through Nginx and the loopback-only backend test port.
- `docker compose config --quiet`: passed for both the normal stack and the opt-in `test` profile.
- Final runtime safety check: the backend was recreated after restoring `ENABLE_TEST_AUTH=false`; the test-login endpoint returned `404`. All three normal services were healthy. The final database query returned zero test-provider users, sessions, posts, messages, follows, and reports after targeted cleanup; the exact test video/poster/VTT/chat/avatar filenames were also removed without targeting real Google data.

## Not verified here

- Native Windows/MSVC: no Windows execution environment was available. Presets, PowerShell instructions, VS Code debugger, Docker setup, and the Windows CI job are configured, but must not be described as a verified native Windows run until CI or the groupmate runs it.
- The optional in-app visual browser connector was not available (`browsers.list()` was empty). This did not replace or weaken the actual system-Chromium Playwright run above; it only means no additional manual connector screenshot was captured.
- Fully closed-browser remote Web Push delivery was not run because no deployment VAPID keys/provider were supplied. PWA installation assets, service-worker/background-tab notifications, browser subscription persistence, and related UI are implemented; this is not reported as a verified remote-push service.

## Temporary test environment

Early native verification used the official MongoDB 8.0.30 Linux binary bound only to `127.0.0.1:27018`, with data under ignored `out/test-mongodb` and separate databases ending in `_test`. Final verification also exercised the Compose `mongo-test` profile on the same loopback port. Test authentication used a disposable key only while API/browser tests ran; the normal runtime was restored with `ENABLE_TEST_AUTH=false` afterward. The test-only database/container was removed after verification. Real Google login now succeeds with the supplied Web Client ID. The saved Google avatar URL returned a valid 96×96 JPEG; the UI additionally uses `Referrer-Policy: no-referrer` and a tested local fallback.

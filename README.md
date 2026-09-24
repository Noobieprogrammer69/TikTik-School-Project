# TikTik school project — C++20, MongoDB, and React

This repository contains the completed TikTok school application: a React/Vite JavaScript frontend, one C++20 Drogon backend, a clean MongoDB database, persistent local media storage, verified Google login, editable/private profiles, follow requests, For You/Following feeds, hashtags, saved videos, watch history, sharing, moderation, richer comments, searchable realtime private chat, notification controls, creator analytics, captions, custom covers, 360p/720p video variants, local drafts, installable PWA support, account export/deletion, and a remembered light/dark theme. The browser never connects directly to MongoDB.

If you have never set up a coding project, follow **Phase 1 through Phase 4** exactly. Docker is the easiest and most consistent path for both group members.

## What you need to give or do

I implemented everything that can be completed from the supplied files. One platform-specific verification remains:

1. **One real Windows run.** Linux and Docker were built and tested here. Your groupmate or GitHub Actions must run the MSVC job before calling native Windows verified.
2. **Choose moderators.** After signing in, copy the stable ID from your profile URL and add it to `ADMIN_USER_IDS` in `.env`. Multiple IDs are comma-separated.
3. **Revoke the old Sanity token.** The legacy credential file was removed, but a deleted file does not invalidate a previously issued token. Remove that token in the old Sanity project dashboard if it still exists.

The Google Client ID is configured and real Google login has been exercised successfully. This project intentionally starts with a clean MongoDB database; no historical cloud dataset or import tooling is retained.

Do not send passwords, private tokens, or MongoDB credentials in chat. Put local values only in `.env`, which Git ignores.

## Phase 1 — install the basic tools

### Omarchy Linux

Open Terminal and run:

```bash
omarchy pkg add docker docker-compose git
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
```

Then **log out of Omarchy and log back in**. This is required for the new Docker group to take effect. Confirm:

```bash
docker version
docker compose version
```

If `docker version` says “permission denied … docker.sock”, you have not logged out/in yet. The full Docker build and healthy three-service stack were verified on the audited Omarchy machine using Docker 29.7.2 and Compose 5.5.0.

### Windows 10/11

This Docker path is not a native Windows C++ build; it runs the same Linux containers on Windows.

1. Install **Git for Windows**.
2. Install **Docker Desktop** and select its WSL2 backend when prompted.
3. Restart Windows if Docker Desktop asks.
4. Open PowerShell and confirm:

```powershell
git --version
docker version
docker compose version
```

5. In Docker Desktop, wait until the engine status says it is running.

## Phase 2 — create your private configuration

Open the repository folder in VS Code. Open its integrated terminal (`Terminal` → `New Terminal`).

Omarchy/Linux:

```bash
cp .env.example .env
code .env
```

Windows PowerShell:

```powershell
Copy-Item .env.example .env
code .env
```

In `.env`:

1. Replace `MONGO_ROOT_PASSWORD` with a long random letters-and-numbers value. Do not reuse an email/school password.
2. Complete the Google setup below and replace `GOOGLE_CLIENT_ID`.
3. Leave `COOKIE_SECURE=false` for local `http://localhost`.
4. Leave `ENABLE_TEST_AUTH=false`. That route is for automated tests only.

### Create the Google Client ID, click by click

1. Sign in to Google Cloud Console.
2. Create a project or select your school project.
3. Open **Google Auth Platform** / **OAuth consent screen**.
4. Configure the app name and support email. For a school demo, use testing mode and add each group member under **Test users**.
5. Open **Clients** / **Credentials** and choose **Create OAuth client ID**.
6. Choose **Web application**.
7. Under **Authorized JavaScript origins**, add exactly `http://localhost:3000`.
8. Create it and copy the value ending in `.apps.googleusercontent.com` into `.env` as `GOOGLE_CLIENT_ID`.
9. Do not put a Google client secret in this repository; this Google Identity Services flow does not use one.

## Phase 3 — start the complete application (recommended)

From the repository root. If Docker works without `sudo`, use:

```bash
docker compose up -d --build
```

If Omarchy still reports permission denied for `/var/run/docker.sock`, use this immediately:

```bash
sudo docker compose up -d --build
```

The first build downloads pinned C++ packages and may take 10–30 minutes. Wait until the terminal shows MongoDB, backend, and frontend as healthy. Open:

**http://localhost:3000**

The browser never connects to MongoDB. Nginx serves React and forwards `/api` and the WebSocket connection to Drogon. To stop the application without deleting data:

```bash
docker compose down
```

Database contents remain in `mongo_data`; uploaded videos remain in `media_data`.

Useful checks in a second terminal:

```bash
docker compose ps
docker compose logs backend
docker compose logs frontend
```

## Phase 4 — daily student workflow in VS Code

When VS Code asks, install the workspace's recommended extensions. They cover C++, CMake, ESLint, Playwright, Docker, and MongoDB.

- `Terminal` → `Run Task` → **Docker: start application** starts all services.
- **Frontend: check** runs lint, unit tests, and the production build.
- **C++: build** asks for your operating-system preset and builds the backend.
- The Run and Debug panel contains Linux GDB, Windows MSVC, and frontend Chrome configurations.

The main folders are:

| Path | Purpose |
|---|---|
| `src/` | React JavaScript/JSX UI |
| `backend/src/` | Drogon routes, Google verification, and MongoDB store |
| `backend/include/` | C++ interfaces |
| `backend/tests/`, `tests/` | C++, API, and browser tests |
| `docs/` | audit, feature checklist, API, migration, verification, presentation |
| `compose.yaml` | MongoDB/backend/frontend services and persistent volumes |

### Using chat and dark mode

- Sign in, open another user's profile, and select **Message**, or open **Messages** from the navigation.
- The inbox shows recent conversations and unread counts. WebSockets deliver messages, typing state, and online presence immediately; polling remains as a recovery fallback.
- The chat history has its own touch-friendly scroll area. Background refreshes preserve your position while reading older messages, and **Jump to latest** appears instead of pulling you away when new activity arrives.
- Messages are private to the two participants and persist in MongoDB. The C++ server—not the browser—sets the sender ID.
- Messages support replies, emoji reactions, sender deletion, image attachments, conversation mute/archive, blocking, and reporting.
- Select the moon/sun button in the top navigation to change appearance. The first visit follows the operating-system theme; later visits remember the selected theme in that browser.

### Social activity and notifications

- Open another user's profile to follow/unfollow, message, or privately report the account. A private account receives a follow request that it can accept or decline in Settings.
- Use **For You** for simple interest-based ranking or **Following** for only followed creators. Topic and hashtag filters use paginated infinite loading.
- On a video detail page, authenticated users can save/share, like comments, reply, and edit/delete their own text. The video owner can pin exactly one comment and remove comments/replies from their video.
- The bell in the navigation opens the notification center. It records follows/requests/acceptances, mentions, new uploads from followed users, video shares, video/comment likes, comments, replies, pins, and messages. Activity can be filtered, and new events appear as a dismissible live toast while the app is open.
- **Library** contains saved videos and watch history. **Settings** changes the display name, bio, avatar, privacy, messaging/comment permissions, notification categories, sessions, desktop notifications, and install status. It also shows creator analytics and security activity and provides account export/deletion.
- IDs listed in `ADMIN_USER_IDS` see **Moderation**, where reports can be reviewed/resolved, accounts suspended/restored, and usage/topic counts inspected.
- Undo operations such as unfollow, unlike, and unpin remove their matching state notification. Reports never notify the reported account.

### Creator, safety, and account tools

- The upload studio reports progress, supports cancellation/retry, saves a private IndexedDB draft, accepts WebVTT captions and a custom JPEG/PNG/WebP cover, and can capture the current preview frame as the cover.
- When media processing is enabled, the C++ service creates 360p and 720p MP4 variants. The player uses a suitable automatic quality and also offers a manual selector. This is file-based quality selection, not HLS streaming.
- The video menu includes **Not interested** and **Hide creator**, both with Undo, to improve the signed-in user's recommendations.
- Chat can load older history without jumping the scroll position, search messages, edit sent text, report messages, and open image attachments in a lightbox.
- Desktop notifications work while the application is open or in a background tab. The server stores standards-based push subscriptions, but fully closed-browser remote delivery still requires a VAPID/web-push sender, which is intentionally documented as a remaining deployment integration.

### Back up and restore your school demo

Create a backup before a presentation or a risky change:

```bash
./scripts/backup.sh
```

The command prints the new folder under ignored `backups/`. It contains the MongoDB archive and media files. Restore one specific backup only when you intentionally want to replace the current database:

```bash
./scripts/restore.sh backups/20260924T120000Z
```

Windows PowerShell equivalents:

```powershell
.\scripts\Backup.ps1
.\scripts\Restore.ps1 -Source backups\20260924T120000Z
```

Restore uses MongoDB `--drop`, so make a fresh backup first. These scripts operate on the running Compose services and never use the removed Sanity project.

## Native Omarchy C++ build (without a backend container)

This builds/runs C++ directly on Omarchy. The steps below were chosen for Arch/Omarchy; do not use Ubuntu `apt` commands.

1. Install build tools. The audit confirmed current Arch repositories contain CMake, Ninja, GCC/Clang, Git, Docker/Compose, Node, and npm:

```bash
omarchy pkg add base-devel cmake ninja git curl zip unzip tar pkgconf autoconf automake libtool gdb nodejs npm ffmpeg
```

2. Create the local vcpkg checkout and use the exact pinned commit:

```bash
mkdir -p .deps
git clone https://github.com/microsoft/vcpkg.git .deps/vcpkg
git -C .deps/vcpkg checkout --detach a1cae005c39be7b18ba319fced856b68d7276271
./.deps/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$PWD/.deps/vcpkg"
```

3. Start only MongoDB through Compose:

```bash
docker compose up -d mongo
```

4. In `.env`, set `MONGODB_URI` with the same Compose username/password, for example:

```text
MONGODB_URI=mongodb://tiktokadmin:YOUR_PASSWORD@127.0.0.1:27017/tiktok_clone?authSource=admin
```

Use a letters-and-numbers password in local development; otherwise URL-encode reserved characters.

5. Configure and build (first run downloads dependencies):

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 2
```

6. Load `.env` into this terminal and run C++:

```bash
set -a
source .env
set +a
./out/build/linux-debug/tiktok_backend
```

7. Open a **second** terminal and run the frontend:

```bash
npm ci
npm run dev
```

8. Open `http://localhost:3000`.

You may set `CMAKE_CXX_COMPILER=clang++` in a personal `CMakeUserPresets.json` if you prefer Clang; do not edit the shared GCC preset just for one machine.

## Native Windows/MSVC build (not WSL and not Linux containers)

1. Install **Visual Studio 2022 Build Tools** with the **Desktop development with C++** workload, MSVC x64 tools, Windows SDK, and CMake tools.
2. Install Git for Windows and Node.js 24 LTS (24.15.0 or newer).
3. Install FFmpeg and make sure `ffmpeg -version` works in Developer PowerShell.
4. Install MongoDB Community Server for Windows as a Windows service, or start only the Compose MongoDB with `docker compose up -d mongo` and use its authenticated URI.
5. Open **Developer PowerShell for VS 2022**.
6. Go to the repository and run:

```powershell
New-Item -ItemType Directory -Force .deps | Out-Null
git clone https://github.com/microsoft/vcpkg.git .deps\vcpkg
git -C .deps\vcpkg checkout --detach a1cae005c39be7b18ba319fced856b68d7276271
.\.deps\vcpkg\bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = (Resolve-Path .\.deps\vcpkg).Path
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 2
```

7. Copy/configure `.env` as described in Phase 2.
8. Load the file into the current PowerShell process:

```powershell
Get-Content .env | Where-Object { $_ -match '^[^#][^=]*=' } | ForEach-Object {
  $name, $value = $_ -split '=', 2
  [Environment]::SetEnvironmentVariable($name.Trim(), $value.Trim(), 'Process')
}
```

9. Start the backend:

```powershell
.\out\build\windows-debug\Debug\tiktok_backend.exe
```

10. In another PowerShell terminal:

```powershell
npm ci
npm run dev
```

11. Open `http://localhost:3000`.

Running `docker compose up` on Windows verifies the Linux-container build, not native MSVC. Record the output of the native `cmake`, build, and test commands separately.

## Tests

Frontend checks (Linux or Windows):

```bash
npm run lint
npm test
npm run build
```

C++ unit tests, Linux:

```bash
ctest --preset linux-debug -LE integration
```

C++ unit tests, Windows Developer PowerShell:

```powershell
ctest --preset windows-debug -LE integration
```

MongoDB integration tests require a database reserved for tests:

```bash
docker compose --profile test up -d --wait mongo-test
TIKTOK_TEST_MONGODB_URI=mongodb://127.0.0.1:27018 ./out/build/linux-debug/tiktok_mongo_integration_tests
docker compose --profile test rm -sf mongo-test
```

The `mongo-test` service stores data in temporary memory and never touches the normal `mongo_data` volume.

Windows Developer PowerShell uses the same disposable database service and the MSVC executable:

```powershell
docker compose --profile test up -d --wait mongo-test
$env:TIKTOK_TEST_MONGODB_URI = 'mongodb://127.0.0.1:27018'
.\out\build\windows-debug\Debug\tiktok_mongo_integration_tests.exe
docker compose --profile test rm -sf mongo-test
```

HTTP and browser tests require a running backend with `ENABLE_TEST_AUTH=true` and a 24+ character disposable `TEST_AUTH_KEY`. The API test also exercises moderation when the test administrator is configured. Never use these settings outside isolated local/CI testing:

```bash
ENABLE_TEST_AUTH=true TEST_AUTH_KEY=your-disposable-24-character-test-key ADMIN_USER_IDS=test:api-alice docker compose up -d --force-recreate backend frontend
TEST_AUTH_KEY=your-disposable-24-character-test-key npm run test:api
TEST_AUTH_KEY=your-disposable-24-character-test-key npm run test:api-next
npx playwright install chromium
TEST_AUTH_KEY=your-disposable-24-character-test-key npm run test:e2e
ENABLE_TEST_AUTH=false TEST_AUTH_KEY= docker compose up -d --force-recreate backend
```

With FFmpeg processing enabled, the focused media test accepts a small real video path:

```bash
TEST_AUTH_KEY=your-disposable-24-character-test-key \
TEST_VIDEO_FILE=/absolute/path/to/small-test-video.webm npm run test:media
```

The automated GitHub Actions workflow configures Linux, isolated MongoDB/API/browser verification, and a native Windows MSVC build. A workflow is only evidence after it actually runs and passes.

## Configuration reference

| Variable | Required | Meaning |
|---|---|---|
| `MONGODB_URI` | yes | Server-only Mongo connection string |
| `MONGODB_DATABASE` | yes | Database name; default `tiktok_clone` |
| `MEDIA_ROOT` | yes | Persistent video directory |
| `GOOGLE_CLIENT_ID` | for real login | public OAuth Web Client ID; not a secret |
| `FRONTEND_ORIGIN` | yes | exact allowed browser origin |
| `BACKEND_LISTEN_ADDRESS`, `BACKEND_PORT` | yes | Drogon listener, defaults `0.0.0.0:8080` |
| `MAX_UPLOAD_BYTES` | yes | server upload ceiling, default 2 GiB and hard maximum 16 GiB; requests over 16 MiB are temporarily file-backed rather than fully buffered in RAM |
| `SESSION_TTL_SECONDS` | yes | server session lifetime, default 7 days |
| `LOGIN_RATE_LIMIT`, `LOGIN_RATE_WINDOW_SECONDS` | yes | Google login attempts per source/window |
| `COOKIE_SECURE` | yes | `false` for local HTTP; `true` for HTTPS |
| `ENABLE_TEST_AUTH`, `TEST_AUTH_KEY` | tests only | disabled test login gate |
| `MONGO_ROOT_USERNAME`, `MONGO_ROOT_PASSWORD`, `MONGO_PORT` | Compose | Mongo container bootstrap/localhost port |
| `FRONTEND_PORT` | Compose | browser port, default 3000 |
| `BACKEND_HOST_PORT` | Compose | loopback-only direct API/debug port, default 8080 |
| `ADMIN_USER_IDS` | optional | comma-separated stable user IDs allowed to use moderation routes/UI |
| `ENABLE_MEDIA_PROCESSING`, `FFMPEG_BINARY` | optional | enable thumbnails/MP4 normalization and locate FFmpeg |

## Documentation and evidence

- [Original repository audit](docs/PROJECT_AUDIT.md)
- [Completed feature checklist](docs/FEATURE_CHECKLIST.md)
- [Architecture and design decisions](docs/MIGRATION_ARCHITECTURE.md)
- [C++ API reference](docs/API.md)
- [Actual test report and unverified items](docs/VERIFICATION.md)
- [School presentation walkthrough](docs/DEMO_WALKTHROUGH.md)
- [Twenty-upgrade implementation notes](docs/UPGRADE_PASS.md)

## Common problems

- **Login not configured:** set `GOOGLE_CLIENT_ID`, recreate/restart the backend container, and ensure the origin is exactly `http://localhost:3000`.
- **Docker permission denied on Omarchy:** run the Phase 1 group command, log out, and log in again.
- **CMake cannot find vcpkg:** set `VCPKG_ROOT` in the same terminal before `cmake --preset ...`. Restart VS Code from that terminal so the extension inherits it.
- **Backend says MongoDB unavailable:** run `docker compose ps`, verify Mongo is healthy, and compare the URI password with `.env`.
- **Cookie disappears:** `COOKIE_SECURE` must be `false` for local HTTP. Use `true` only with HTTPS.
- **A video upload is rejected:** use MP4/WebM/Ogg whose real bytes match its extension and MIME, and keep it below `MAX_UPLOAD_BYTES`.
- **No thumbnail is generated:** Docker already includes FFmpeg. For a native run, install FFmpeg, confirm `ffmpeg -version`, and verify `ENABLE_MEDIA_PROCESSING=true`.
- **A first C++ container build loses a GitHub download:** rerun `docker compose build backend`; downloads are retried. For a persistently unreliable school network, put the matching verified vcpkg source archive in the Git-ignored `docker-cache/` folder as described by the filename error, then rebuild.

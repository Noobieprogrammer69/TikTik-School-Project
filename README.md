# RippleNest

RippleNest is our short-form video social platform for a school project. It is a place where people can post clips, discover creators, talk to friends, and build communities around the things they enjoy.

The project began as a TypeScript/Sanity TikTok clone. We kept the parts we liked about the original interface, moved the application logic into C++20, replaced Sanity with MongoDB, and continued building from there. It is now its own project rather than a copy of the original tutorial.

## What we built

- Google sign-in with server-verified sessions
- For You and Following feeds
- Video uploads, covers, captions, 360p/720p versions, playback, seeking, and looping
- Profiles, private accounts, follow requests, likes, comments, replies, and mentions
- Saved videos, watch history, sharing, search, hashtags, and topics
- Private realtime chat with replies, reactions, images, editing, and message search
- Notifications for social activity, including follows, shares, comments, and mentions
- Creator analytics, privacy controls, account export, and session management
- Reporting, blocking, muting, and an administrator moderation screen
- Responsive light and dark themes
- A small installable PWA for desktop and mobile browsers

Repeated plays count as new views. A signed-in account still gets only one Library history entry per video, while the public view counter increases whenever a video is opened, played from the feed, or completes another loop.

## How it is put together

| Part | Technology |
|---|---|
| Frontend | React 19, JavaScript/JSX, Vite, React Router, Tailwind CSS |
| Backend | C++20 and Drogon |
| Database | MongoDB with the official `mongocxx` driver |
| Authentication | Google Identity Services and C++ token verification |
| Realtime updates | Authenticated Drogon WebSockets |
| Video processing | FFmpeg |
| Builds | CMake, vcpkg, npm, and Docker Compose |
| Tests | Catch2, Vitest, API integration tests, and Playwright |

The browser never talks directly to MongoDB. React calls the C++ API, and the C++ backend is responsible for authentication, permissions, database access, uploads, and media delivery.

## Running RippleNest with Docker

Docker is the normal development setup because it behaves the same on Omarchy Linux and Windows.

### Requirements

- Git
- Docker with Docker Compose
- A Google OAuth Web Client ID

On Omarchy:

```bash
omarchy pkg add docker docker-compose git
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
```

Log out and back in after adding the Docker group.

On Windows, we use Git for Windows and Docker Desktop with the WSL2 backend. This runs the Linux containers; the separate native MSVC setup is described later.

### Local configuration

Create a private `.env` file:

```bash
cp .env.example .env
```

PowerShell:

```powershell
Copy-Item .env.example .env
```

The two values that normally need changing are:

```dotenv
MONGO_ROOT_PASSWORD=use-a-long-random-password-here
GOOGLE_CLIENT_ID=your-client-id.apps.googleusercontent.com
```

The real `.env` file is ignored by Git. We never commit MongoDB passwords, test keys, or other private credentials.

### Google sign-in

In Google Cloud Console:

1. Create or select a project.
2. Configure the Google Auth Platform consent screen.
3. Add the accounts used for development as test users.
4. Create an OAuth client with the **Web application** type.
5. Add `http://localhost:3000` as an authorized JavaScript origin.
6. Put the generated client ID in `.env` as `GOOGLE_CLIENT_ID`.

This login flow does not need a Google client secret in the frontend or repository.

### Start the app

```bash
docker compose up -d --build
```

The first C++ build takes longer because vcpkg downloads and compiles the pinned dependencies. Once all three services are healthy, RippleNest is available at:

**http://localhost:3000**

Useful commands:

```bash
docker compose ps
docker compose logs -f backend
docker compose logs -f frontend
docker compose down
```

`docker compose down` keeps the database and uploaded media. We avoid `docker compose down -v` unless we deliberately want to erase both volumes.

After changing source code, the normal rebuild command is:

```bash
docker compose up -d --build
```

## Working without the full Docker stack

### Frontend

With the C++ backend already running:

```bash
npm ci
npm run dev
```

The Vite development server runs at `http://localhost:3000` and proxies `/api` to the backend.

### Native C++ on Omarchy

Install the native tools:

```bash
omarchy pkg add base-devel cmake ninja git curl zip unzip tar pkgconf autoconf automake libtool gdb nodejs npm ffmpeg
```

Create the pinned vcpkg checkout once:

```bash
mkdir -p .deps
git clone https://github.com/microsoft/vcpkg.git .deps/vcpkg
git -C .deps/vcpkg checkout --detach a1cae005c39be7b18ba319fced856b68d7276271
./.deps/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$PWD/.deps/vcpkg"
```

Start MongoDB, configure, build, and run:

```bash
docker compose up -d mongo
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 2
set -a
source .env
set +a
./out/build/linux-debug/tiktok_backend
```

For this setup, `MONGODB_URI` in `.env` must point to the Compose MongoDB instance and include the same username/password.

### Native C++ on Windows

The native Windows build uses Visual Studio 2022 Build Tools with the **Desktop development with C++** workload, CMake, Git, Node.js 24, FFmpeg, and the same pinned vcpkg commit.

From Developer PowerShell:

```powershell
New-Item -ItemType Directory -Force .deps | Out-Null
git clone https://github.com/microsoft/vcpkg.git .deps\vcpkg
git -C .deps\vcpkg checkout --detach a1cae005c39be7b18ba319fced856b68d7276271
.\.deps\vcpkg\bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = (Resolve-Path .\.deps\vcpkg).Path
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 2
ctest --preset windows-debug -LE integration
```

The executable is normally created at:

```powershell
.\out\build\windows-debug\Debug\tiktok_backend.exe
```

Docker Desktop and a native MSVC build are different checks. Docker proves that the Linux containers work on Windows; the commands above test the actual Windows binary.

## Tests

Frontend checks:

```bash
npm run lint
npm test
npm run build
```

C++ unit tests on Linux:

```bash
ctest --preset linux-debug -LE integration
```

MongoDB integration tests use the isolated `mongo-test` service rather than the normal database:

```bash
docker compose --profile test up -d --wait mongo-test
TIKTOK_TEST_MONGODB_URI=mongodb://127.0.0.1:27018 ./out/build/linux-debug/tiktok_mongo_integration_tests
docker compose --profile test rm -sf mongo-test
```

The API and Playwright suites use the deliberately disabled test-login route. The exact test setup and the results we actually obtained are recorded in [docs/VERIFICATION.md](docs/VERIFICATION.md).

## Backups

The backup contains both the MongoDB archive and uploaded media:

```bash
./scripts/backup.sh
```

PowerShell:

```powershell
.\scripts\Backup.ps1
```

Restore commands replace the current database, so we create a fresh backup first:

```bash
./scripts/restore.sh backups/20260924T120000Z
```

```powershell
.\scripts\Restore.ps1 -Source backups\20260924T120000Z
```

## Project layout

```text
backend/        C++ API, MongoDB store, authentication, WebSockets, and tests
src/            React pages, components, API client, and frontend tests
public/         PWA manifest, service worker, and static assets
tests/          HTTP, media-processing, and browser journeys
docs/           Architecture, API, migration notes, and verification evidence
scripts/        Backup and restore helpers for Bash and PowerShell
compose.yaml    MongoDB, backend, and frontend containers
```

## A few decisions we made

- Google is the only real login method because it was already the provider used by the original project.
- MongoDB starts clean. We did not import the old Sanity dataset.
- Large videos live in the media volume; MongoDB stores their metadata and relationships.
- The recommendation score is intentionally understandable instead of pretending to be machine learning.
- React remains in the browser. All trusted business logic and database access live in C++.
- Desktop notifications work while RippleNest is open or in a background tab. Fully closed-browser delivery would still need production VAPID keys and a Web Push sender.

## Documentation

- [API reference](docs/API.md)
- [Architecture and migration](docs/MIGRATION_ARCHITECTURE.md)
- [Feature checklist](docs/FEATURE_CHECKLIST.md)
- [Verification report](docs/VERIFICATION.md)
- [Presentation walkthrough](docs/DEMO_WALKTHROUGH.md)
- [Upgrade notes](docs/UPGRADE_PASS.md)

## Authors

Created by Jover Vergara and the project group for our school presentation.

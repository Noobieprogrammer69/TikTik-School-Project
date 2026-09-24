#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
destination="${1:-$project_dir/backups/$stamp}"
mkdir -p "$destination"

docker compose --project-directory "$project_dir" exec -T mongo sh -lc \
  'mongodump --quiet --username "$MONGO_INITDB_ROOT_USERNAME" --password "$MONGO_INITDB_ROOT_PASSWORD" --authenticationDatabase admin --db tiktok_clone --archive --gzip' \
  > "$destination/mongodb.archive.gz"
docker compose --project-directory "$project_dir" cp backend:/data/media "$destination/media"

printf 'Backup created at %s\n' "$destination"

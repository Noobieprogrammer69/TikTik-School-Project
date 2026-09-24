#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  printf 'Usage: %s backups/YYYYMMDDTHHMMSSZ\n' "$0" >&2
  exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="$(realpath "$1")"
test -f "$source_dir/mongodb.archive.gz"

docker compose --project-directory "$project_dir" cp "$source_dir/mongodb.archive.gz" mongo:/tmp/tiktik-restore.archive.gz
docker compose --project-directory "$project_dir" exec -T mongo sh -lc \
  'mongorestore --username "$MONGO_INITDB_ROOT_USERNAME" --password "$MONGO_INITDB_ROOT_PASSWORD" --authenticationDatabase admin --archive=/tmp/tiktik-restore.archive.gz --gzip --drop'
if [[ -d "$source_dir/media" ]]; then
  docker compose --project-directory "$project_dir" cp "$source_dir/media/." backend:/data/media
fi

printf 'Restore completed from %s\n' "$source_dir"

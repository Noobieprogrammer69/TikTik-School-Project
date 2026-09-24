param([string]$Destination = "")
$ErrorActionPreference = "Stop"
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Destination) {
  $Stamp = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")
  $Destination = Join-Path $ProjectDir "backups\$Stamp"
}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

docker compose --project-directory $ProjectDir exec -T mongo sh -lc 'mongodump --quiet --username "$MONGO_INITDB_ROOT_USERNAME" --password "$MONGO_INITDB_ROOT_PASSWORD" --authenticationDatabase admin --db tiktok_clone --archive=/tmp/tiktik-backup.gz --gzip'
docker compose --project-directory $ProjectDir cp mongo:/tmp/tiktik-backup.gz (Join-Path $Destination "mongodb.archive.gz")
docker compose --project-directory $ProjectDir cp backend:/data/media (Join-Path $Destination "media")
Write-Host "Backup created at $Destination"

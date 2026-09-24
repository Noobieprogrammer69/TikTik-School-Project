param([Parameter(Mandatory=$true)][string]$Source)
$ErrorActionPreference = "Stop"
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SourceDir = (Resolve-Path $Source).Path
$Archive = Join-Path $SourceDir "mongodb.archive.gz"
if (-not (Test-Path $Archive)) { throw "mongodb.archive.gz was not found in $SourceDir" }

docker compose --project-directory $ProjectDir cp $Archive mongo:/tmp/tiktik-restore.archive.gz
docker compose --project-directory $ProjectDir exec -T mongo sh -lc 'mongorestore --username "$MONGO_INITDB_ROOT_USERNAME" --password "$MONGO_INITDB_ROOT_PASSWORD" --authenticationDatabase admin --archive=/tmp/tiktik-restore.archive.gz --gzip --drop'
$Media = Join-Path $SourceDir "media"
if (Test-Path $Media) { docker compose --project-directory $ProjectDir cp "$Media\." backend:/data/media }
Write-Host "Restore completed from $SourceDir"

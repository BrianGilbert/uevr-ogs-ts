# sync-core.ps1 — vendor the canonical serializer from ogs-ts-unreal.
# The headers are the same files ogs-ts-unreal proves against the
# standard's conformance corpus (its Test/ binary). Run from repo root.
$ErrorActionPreference = 'Stop'

$src = "../ogs-ts-unreal/Source/OgsTelemetryCore/Public/OgsTelemetry"
$dst = "OgsTelemetry"

if (-not (Test-Path "$src/OgsTelemetryWire.h")) {
    throw "source not found: $src (is ogs-ts-unreal checked out as a sibling?)"
}

$changed = 0
foreach ($f in @("OgsTelemetryCanonical.h", "OgsTelemetrySchema.h",
                 "OgsTelemetryWire.h", "OgsTelemetryFrame.h")) {
    if (-not (Test-Path "$dst/$f") -or
        (Get-FileHash "$src/$f").Hash -ne (Get-FileHash "$dst/$f").Hash) {
        Copy-Item "$src/$f" "$dst/$f"
        Write-Host "updated: $dst/$f"
        $changed = 1
    }
}
if ($changed -eq 0) { Write-Host "core up to date" }
exit 0

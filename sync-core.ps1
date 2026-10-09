# sync-core.ps1 — vendor the corpus-locked serializer from ogsa-ts-unreal.
# The two headers are the same files ogsa-ts-unreal proves against the
# standard's conformance corpus (its Test/ binary). Run from repo root.
$ErrorActionPreference = 'Stop'

$src = "../ogsa-ts-unreal/Source/OgsaTelemetryCore/Public/OgsaTelemetry"
$dst = "OgsaTelemetry"

if (-not (Test-Path "$src/OgsaTelemetryWire.h")) {
    throw "source not found: $src (is ogsa-ts-unreal checked out as a sibling?)"
}

$changed = 0
foreach ($f in @("OgsaTelemetryWire.h", "OgsaTelemetryFrame.h")) {
    if (-not (Test-Path "$dst/$f") -or
        (Get-FileHash "$src/$f").Hash -ne (Get-FileHash "$dst/$f").Hash) {
        Copy-Item "$src/$f" "$dst/$f"
        Write-Host "updated: $dst/$f"
        $changed = 1
    }
}
if ($changed -eq 0) { Write-Host "core up to date" }
exit 0

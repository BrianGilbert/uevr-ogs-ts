# ogsa-ts-uevr

[UEVR](https://github.com/praydog/UEVR) plugin that publishes
[OGSA Telemetry Standard (OGSA-TS)](../OGSA-Telemetry-Standard) motion
telemetry from **any UE4/UE5 game UEVR injects** — zero game
modification, LAN-only UDP multicast. Motion platforms, haptic hardware,
and dashboards on the local network can follow the game.

Status: **smoke-verified** — the wire path (socket → corpus-locked
serializer → multicast) and the full UE math chain (FRotator →
`FRotator::Quaternion` transcription → OGSA-TS frame conversion) pass
standalone checks (`Test/smoke.cpp`, 6/6) and were observed end-to-end
on a real receiver (`ogsa_smoke` → TelemetryReceive). **In-game testing
pending** (see Testing below).

## What it tracks — the pawn, not the headset

The motion object is the **local pawn** — the body the player controls
(`get_local_pawn`). Not the HMD/camera: the camera is engine-smoothed
and a rig following it would feed platform motion back into telemetry.
The pawn is the rigid body whose inertial frame a motion rig reproduces.
(If a game's pawn is a vehicle, set `objectType=vehicle` in the config.)

## Install

1. Build (or take from CI artifacts): `cmake -B build && cmake --build
   build --config Release` → `build/Release/ogsa_telemetry.dll`
2. Drop the DLL into your UEVR plugins folder — global
   (`<uevr>/plugins/`) or per-game (`<game>/uevr/plugins/`).
3. Optional config: `ogsa_telemetry.ini` beside the DLL:

```ini
group=239.255.17.17
port=40123
ttl=1
motionHz=60
objectName=player
objectType=humanoid    ; humanoid|vehicle|aircraft|watercraft|spacecraft|camera
gameName=              ; default: the game's exe name
engine=ue5             ; ue5 (FVector doubles, default) | ue4 (floats)
```

Pair with a receiver, e.g. ogsa-ts-dotnet's
`dotnet run --project src/TelemetryReceive`.

## Testing

- **Standalone (no UEVR)**: `build/Release/ogsa_smoke.exe` — conversion
  assertions + 150 synthetic ticks to the multicast group; exit 0 = pass.
  Run TelemetryReceive beside it to watch `synthetic_pawn` on the wire.
- **In-game**: inject UEVR into a UE5 game, confirm in the UEVR log
  (`[ogsa] OGSA-TS telemetry loaded: pawn ...`), watch TelemetryReceive:
  the pawn should hold continuity through camera cuts and carry motion
  while the view lags — that's the signal a rig wants.

## Layout

| Path | Contents |
|---|---|
| `plugin/` | UEVR glue (pawn sampling, K2 calls, config) + engine-agnostic `ogsa_publisher` |
| `OgsaTelemetry/` | vendored corpus-locked serializer from ogsa-ts-unreal (`sync-core.ps1`) |
| `vendor/uevr/API.h` | UEVR plugin C API (MIT, praydog; pinned @4ee5c6b) |
| `Test/smoke.cpp` | standalone wire-path + math verification |

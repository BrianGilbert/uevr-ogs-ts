# ogsa-ts-uevr

[UEVR](https://github.com/praydog/UEVR) plugin that publishes
[OGSA Telemetry Standard (OGSA-TS)](../OGSA-Telemetry-Standard) motion
telemetry from **any UE4/UE5 game UEVR injects** — zero game
modification, LAN-only UDP multicast. Motion platforms, haptic hardware,
and dashboards on the local network can follow the game.

Status: **in-game verified (UE4)** — first live session on Stray
(UE4) walked the cat around a room while a receiver watched the wire;
that session exposed and fixed a FVector ABI footgun (UE4 floats read
as UE5 doubles → position 0,0,~1e30). The ABI is now auto-detected at
runtime (see Install). Standalone checks: `Test/smoke.cpp`, 6/6.

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
engine=auto            ; auto (default: probes FVector size) | ue5 | ue4
```

Pair with a receiver, e.g. ogsa-ts-dotnet's
`dotnet run --project src/TelemetryReceive`.

## Testing

- **Standalone (no UEVR)**: `build/Release/ogsa_smoke.exe` — conversion
  assertions + 150 synthetic ticks to the multicast group; exit 0 = pass.
  Run TelemetryReceive beside it to watch `synthetic_pawn` on the wire.
- **In-game**: inject UEVR into the game, confirm in the UEVR log
  (`[ogsa] OGSA-TS telemetry loaded: …` and the follow-up
  `[ogsa] FVector ABI auto-detected: params size N → UE4|UE5` line),
  watch TelemetryReceive: the pawn should hold continuity through
  camera cuts and carry motion while the view lags — that's the signal
  a rig wants. (Validated this way on Stray/UE4.)

## Layout

| Path | Contents |
|---|---|
| `plugin/` | UEVR glue (pawn sampling, K2 calls, config) + engine-agnostic `ogsa_publisher` |
| `OgsaTelemetry/` | vendored corpus-locked serializer from ogsa-ts-unreal (`sync-core.ps1`) |
| `vendor/uevr/API.h` | UEVR plugin C API (MIT, praydog; pinned @4ee5c6b) |
| `Test/smoke.cpp` | standalone wire-path + math verification |

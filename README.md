# uevr-ogsa-ts

[UEVR](https://github.com/praydog/UEVR) plugin that publishes
[OGSA Telemetry Standard (OGSA-TS)](https://github.com/Open-Game-Standards-Alliance/OGSA-Telemetry-Standard) motion
telemetry from **any UE4/UE5 game UEVR injects** — zero game
modification, LAN-only UDP multicast. Motion platforms, haptic hardware,
and dashboards on the local network can follow the game. Motion frames
carry pose **plus body dynamics** (specific force + angular rates — the
direct motion-cueing inputs, derived from the pawn and the engine-measured
velocity).

Support & discussion: [Discord](https://discord.gg/yyCp5D2Ccp)

Status: **in-game verified (UE4)** — first live session on Stray
(UE4) walked the cat around a room while a receiver watched the wire;
that session exposed and fixed a FVector ABI footgun (UE4 floats read
as UE5 doubles → position 0,0,~1e30). The ABI is now auto-detected at
runtime (see Install). Standalone checks: `Test/smoke.cpp`, 14/14
(wire + derived-dynamics assertions).

## What it tracks — the pawn, not the headset

The motion object is the **local pawn** — the body the player controls
(`get_local_pawn`). Not the HMD/camera: the camera is engine-smoothed
and a rig following it would feed platform motion back into telemetry.
The pawn is the rigid body whose inertial frame a motion rig reproduces.
(If a game's pawn is a vehicle, set `objectType=vehicle` in the config.)

## Install

1. Build locally (`cmake -B build && cmake --build build --config
   Release` → `build/Release/ogsa_telemetry.dll`) or grab a prebuilt
   DLL from
   [GitHub Releases](https://github.com/BrianGilbert/uevr-ogsa-ts/releases)
   — CI publishes a CalVer build (`vYYYY.MM.DD`) on every push to main.
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
dynamics=on            ; on (default) | off — bodyDynamics variant in motion frames
gameName=              ; default: the game's exe name, pack/build tokens
                       ; stripped ("Game-Win64-Shipping" -> "Game")
engine=auto            ; auto (default: probes FVector size) | ue5 | ue4
```

The tracked pawn's class name is surfaced automatically as the object's
`typeLabel` in discovery (e.g. "BP_PlayerCharacter_C"), re-sent when
possession changes; `objectName` stays the stable matching key.

## Body dynamics on the wire

Every motion frame carries the schema's `bodyDynamics` union variant —
the two quantities motion cueing is built around, derived per engine
tick from the sampled pose and the pawn's engine-measured velocity
(`AActor::GetVelocity`):

- **specific force** (m/s²), accelerometer-style: `f = a − g`, reading
  +1 g on the up axis at rest (what an IMU reads), rotated into the
  body frame — the direct input of tilt coordination.
- **angular velocity** (rad/s), body frame in the C-4 component sense
  (+x nose-up, +y nose-right, +z right-side-down) — the direct input
  of onset cueing, derived from quaternion deltas (no Euler
  representation-flip artifacts).

Derivation details: velocity comes from the engine when it has one
(else position deltas), acceleration is low-pass filtered (τ = 0.1 s),
and teleports (> 2.5 m per tick) reset the estimator instead of
spiking. Receivers with native accelerometer-class sources get that
bound directly instead; senders that derive still beat receiver-side
Euler differentiation (GameConn `docs/cueing.md`).

Pair with a receiver, e.g. ogsa-ts-dotnet's
`dotnet run --project src/TelemetryReceive`.

### Via Rai Pal

Rai Pal manages per-game UEVR installs, plugin included:

1. Select the game and click **Install UEVR**.
2. Click the three-dots indicator to the right of **Uninstall** and pick
   **Open Mod Folder**.
3. In the folder that opens, create a `plugins` directory and drop
   `ogsa_telemetry.dll` into it (`plugins\ogsa_telemetry.dll`).
4. Click **Run** and load the game — telemetry starts with the first
   pawn, no INI required.

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
| `OgsaTelemetry/` | vendored canonical-encoding serializer from ogsa-ts-unreal (`sync-core.ps1`), corpus-locked |
| `vendor/uevr/API.h` | UEVR plugin C API (MIT, praydog; pinned @4ee5c6b) |
| `Test/smoke.cpp` | standalone wire-path + math verification |

# ogsa-ts-uevr working conventions

- Motion object is the LOCAL PAWN, not the HMD/camera — operator ruling
  2026-10-10, knowledge base projects/uevr C-20261010-MBP-08 (camera is
  engine-smoothed; rig-following the HMD feeds platform motion back).
- `OgsaTelemetry/` is vendored from ../ogsa-ts-unreal
  (Source/OgsaTelemetryCore/Public/OgsaTelemetry) — never hand-edit;
  refresh with ./sync-core.ps1. Corpus-locked: byte-agrees with the
  standard's conformance vectors (ogsa-ts-unreal Test/ proves it).
- `vendor/uevr/API.h` is praydog's plugin API (MIT, separate from
  UEVR's GPL codebase — keep the license header). Pinned to the UEVR
  commit recorded in the header comment; refresh deliberately and
  re-read the changelog for ABI changes.
- UE4 vs UE5 ABI: FVector/FRotator are doubles on UE5 (LWC), floats on
  UE4 — auto-detected at runtime from K2_GetActorLocation's UFunction
  params size (12 = UE4, 24 = UE5); the config `engine=ue4|ue5` forces.
  A wrong ABI silently corrupts pose (UE4 floats read as doubles →
  0,0,~1e30 garbage; observed in-game on Stray, 2026-10-12).
- Product docs describe current state only; history lives in the
  Open-Game-Standards-Alliance knowledge base (knowledge/README.md bridge).

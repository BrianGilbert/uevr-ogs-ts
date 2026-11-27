# 2. A motion frame may go up to a quarter of an interval early

Date: 2026-11-27. Status: accepted, not yet confirmed in a game.

## Decision

A motion frame is sent on a tick that arrives up to a quarter of an interval
before its due time. Without this, a host running close to the motion rate
skipped frames and sent less than the declared rate. The reasons, measurements
and consequences are in `ogs-ts-dotnet`'s
`docs/adr/0003-motion-frames-may-go-early.md`.

The smoke test sends over loopback at frame rates from 59 to 144 with uneven
frame times and counts the motion frames received.

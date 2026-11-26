# 1. Check the engine velocity against movement

Date: 2026-11-26. Status: accepted, not yet confirmed in a game.

## Context

The plugin passes `GetVelocity` to the dynamics calculation, which treats it
as authoritative. Custom vehicle physics can leave it at zero or in another
frame, and a failed call leaves it at zero too. In a simulation of a pawn
accelerating at 10 m/s² with a zero reading, surge stayed at zero and the
estimate kept resetting as if the pawn had jumped to a new place.

## Decision

Every 0.5 s the publisher compares the distance the pawn moved with the
distance the reported velocity explains. A check fails when more than half the
path, plus 0.5 m, is unexplained. Each change of source restarts the
derivation. The check matches the one in `ue4ss-ogs-ts`.

**Three failures in a row** are needed before position deltas take over, and
one passing check brings the engine reading back. One failure was enough at first.
In a `ue4ss-ogs-ts` game recording, brief fallbacks brought back the
surge spikes that engine velocity removes. A reading that's really broken keeps failing, so it still loses,
about a second later. Until then its dynamics are wrong.

**A frozen pose isn't evidence** against a velocity. A failed check in which
the pawn didn't move at all is never counted, however long that lasts: a game
can hold its simulation without reporting a pause, as Hot Wheels Unleashed
does while it shows a keyboard hint, and the velocity stays as it was. A
failed check isn't counted either when the pawn moved less than a tenth of the
distance the velocity explains. In a `ue4ss-ogs-ts` game recording, switching during a frozen pose
caused a surge spike when the pose moved again. At most six of these slightly moving checks in a row
are skipped. Without that limit a velocity more than ten times too big would
never be rejected.

**Only movement restores trust.** A passing check counts when the pawn moved
more than 1 m in it, the distance at which a zero velocity fails. A parked
pawn passes any velocity that reads zero, so without this a reading stuck at
zero was trusted again at every stop.

## Consequences

- A game whose reading fails the check gets the position path, which spikes
  when the pawn's steps don't line up with the engine tick. The debug log
  records the source in use.

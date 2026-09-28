# Targeting System — Candidate Scoring

Status: **implemented.** Documents `USlashTargetingComponent::ScoreCandidate` as it exists in
`Source/Slash/Targeting/SlashTargetingComponent.cpp`. This is a reference for the scoring math
only — see `TargetingSystem_Plan.md` / `TargetingSystem_Spec.md` for the broader system design
(those predate this implementation and describe a different, superseded shape).

## Pipeline

Every tick, `UpdateTargeting` runs three steps:

1. `GatherCandidates()` — broad-phase physics overlap on the `Targetable` object channel
   (`COLLISION_TARGETABLE`), radius `TargetQueryRadius`. Filters to `IsTargetable()` components.
2. `RefreshPersistentTarget()` — scores every candidate via `ScoreCandidate` and keeps the
   highest-scoring one as `PersistentTarget`. Recomputed from scratch every tick; there is no
   separate "hold the lock" state machine — persistence is purely the stickiness bonus below
   outweighing a challenger's score.
3. `DebugDrawAllCandidates()` — draws a box at each candidate's `GetTargetPoint()`, green for
   `PersistentTarget`, red for the rest.

## `ScoreCandidate` parameters

| Parameter | Source | Meaning |
|---|---|---|
| `Candidate` | one entry from `GatherCandidates()` | The target being scored. |
| `Origin` | `Owner->GetActorLocation()` | Scoring reference point — the player. |
| `IntentDir` | `PlayerCameraManager->GetCameraRotation().Vector()`, falling back to `Owner->GetActorForwardVector()` if there's no player controller/camera manager | "Where the player is looking." |
| `Incumbent` | current `PersistentTarget` | Enables the stickiness bonus. |
| `Clamp` | `TargetQueryRadius` | Caps the distance reject gate; see [caveat](#maxdistance-vs-clamp) below. |

## `FSlashTargetWeights`

| Field | Default | Role |
|---|---|---|
| `MaxDistance` | 1400 cm | Distance the falloff curve normalizes against. Deliberately tighter than `TargetQueryRadius` (2000) — this is melee lock-on range, not general awareness range. |
| `MinDot` | 0.5 | Cone cutoff as a dot product, not degrees — see below. |
| `AngleWeight` | 0.65 | Weight of the angle term in the final score. Dominant on purpose: lock-on should favor who the player is facing over who is merely closest. |
| `DistanceWeight` | 0.35 | Weight of the distance term. |
| `StickinessBonus` | 0.2 | Flat bonus if `Candidate == Incumbent`. |
| `bRequireLineOfSight` | true | **Not wired up.** No trace is performed; occluded targets still score normally. |

`TargetPriorityBias` is a separate per-enemy flat offset, `EditAnywhere` on `UTargetableComponent`
itself (not part of the weights struct) — e.g. bias a boss above nearby trash mobs.

## Reject gates

Evaluated in order; any failure returns an unset `TOptional<float>` (rejected — not merely a low
score, excluded entirely from `SelectBest`):

```cpp
if (!Candidate || !Candidate->IsTargetable())              reject;
if (Dist > EffectiveMaxDistance)                           reject;   // see below
if (Dot(IntentDir, DirToTarget) < Weights.MinDot)           reject;   // outside the cone
```

### Why `MinDot` isn't degrees

The dot product of two unit vectors is the cosine of the angle between them: `1` = same direction
(0°), `0` = perpendicular (90°). `MinDot = 0.5` → `acos(0.5) = 60°`, so the cone extends 60° off
the player's forward vector on either side (120° total) — narrow enough that lock-on tracks "who
am I facing," not "who is anywhere near me."

### `MaxDistance` vs `Clamp`

```cpp
EffectiveMaxDistance = Clamp > 0 ? min(Clamp, Weights.MaxDistance) : Weights.MaxDistance;
reject if Dist > EffectiveMaxDistance;
```

The reject gate uses `EffectiveMaxDistance`, but the distance-score calculation below always
normalizes against `Weights.MaxDistance` directly, **not** the clamped value. With the current
defaults `TargetQueryRadius (2000) > Weights.MaxDistance (1400)`, so `Clamp` never actually binds
— `EffectiveMaxDistance` always resolves to `Weights.MaxDistance`, and `TargetQueryRadius` is
purely the broad-phase net cast wider than the scoring cutoff, which is the intended relationship.
The asymmetry only bites if `TargetQueryRadius` is ever set *below* `Weights.MaxDistance`: a
candidate accepted under that tighter `Clamp` would never actually reach `DistanceAlpha = 0`, since
`Dist` can't exceed `Clamp`, which would then be smaller than the value the falloff is normalized
against. Keep `TargetQueryRadius >= Weights.MaxDistance` and this never surfaces.

## Score calculation

```cpp
AngleAlpha    = remap(Dot, [MinDot, 1] -> [0, 1])              // = (Dot - MinDot) / (1 - MinDot)
DistanceAlpha = 1 - clamp(Dist / Weights.MaxDistance, 0, 1)

Score = Weights.AngleWeight    * AngleAlpha
      + Weights.DistanceWeight * DistanceAlpha
      + Candidate->TargetPriorityBias
      + (Candidate == Incumbent ? Weights.StickinessBonus : 0)
```

Since anything reaching this point already satisfied `Dot >= MinDot`, `AngleAlpha` is guaranteed
in `[0, 1]`: dead-on-center → `1`, right at the cone's edge → `0`. Same shape for `DistanceAlpha`:
point-blank → `1`, at `MaxDistance` → `0`.

The angle/distance portion alone is bounded `[0, 1]` — best case (dead-ahead, point-blank) scores
exactly `AngleWeight + DistanceWeight = 1.0`. `TargetPriorityBias` and `StickinessBonus` are
unbounded additive terms on top of that, by design — they're the knobs for "always prefer this
enemy" and "don't flicker off what's already locked."

## Worked example

Player facing dead ahead (`IntentDir` = forward), two candidates:

| | Dist | Angle off-center | Dot | AngleAlpha | DistanceAlpha | Base score |
|---|---|---|---|---|---|---|
| Enemy A | 400 cm | 5° | 0.996 | (0.996−0.5)/0.5 ≈ 0.992 | 1 − 400/1400 ≈ 0.714 | 0.65×0.992 + 0.35×0.714 ≈ **0.895** |
| Enemy B | 100 cm | 55° | 0.574 | (0.574−0.5)/0.5 ≈ 0.148 | 1 − 100/1400 ≈ 0.929 | 0.65×0.148 + 0.35×0.929 ≈ **0.421** |

(At the old 70°-cone default, a candidate 65° off-center would've qualified; at the new 60° cone
it would be rejected outright — `MinDot = 0.5` cuts anything past 60° before scoring even runs.)

A wins despite being farther away — `AngleAlpha` is linear in `Dot` (cosine), not in the raw angle,
so it collapses fast once a candidate drifts toward the cone's edge, and `AngleWeight` now leans
further into that effect. Even with B as the current `Incumbent` (+0.2 → 0.621), the gap to A is
too large for stickiness alone to hold the lock here; that bonus is tuned for close calls, not for
overriding a clearly better-aimed candidate.

## Known gaps

- `bRequireLineOfSight` is declared but not implemented — no trace occurs.
- No hysteresis beyond `StickinessBonus`; a target can be dropped mid-frame if a challenger's score
  is high enough, with no minimum "hold time."

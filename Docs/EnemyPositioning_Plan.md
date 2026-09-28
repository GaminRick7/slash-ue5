# Enemy Positioning — Design Plan

Status: **implemented (Phases 0-4, plus the RVO, telegraph and back-off items from Phase 5).**
`USlashEncounterDirector` (`Enemy/SlashEncounterDirector`), `UEnemyBrainComponent` (`Enemy/EnemyBrainComponent`)
and `AEnemyAIController` follow this document. Divergences: the brain has a `Windup` state between `Approach`
and `Attack` (the Phase 5 telegraph), enemies attack by "pressing" `IA_Attack` into the player's own
`DA_Combo_Sword` tree, and `Approach` steers directly within `PathfindDistance`, only using navmesh
pathing beyond it. `slash.Encounter.Debug 1` draws the ring and token holders.

## The problem

Each enemy independently deciding "run at the player" produces a dogpile: bodies stacked in the same spot, all
swinging simultaneously, no readable threat. The fix is not smarter individual enemies — it is to take the
decision away from them. A central director owns the space around the player and hands each enemy a *goal*;
separately, it hands out permission to attack.

That split is the whole design. Enemies execute, the director decides.

## Goals

- **No clumping.** Enemies distribute around the player rather than converging on one point.
- **Readable pressure.** One or two enemies commit at a time; the rest circle visibly. A crowd should feel
  choreographed, not like a mob.
- **Stable.** Positioning must not jitter or make enemies trade places when the player turns.
- **Cheap.** Positioning is a slow signal. The director runs on a timer at ~10 Hz, never per frame.
- **Reuses what exists.** Enemy attacks should run through `UComboComponent` and `UMeleeWeaponComponent`
  unchanged — both were built as actor components for exactly this.

## Non-goals (for this pass)

- Behaviour Trees. See the note at the end; the brain is a C++ state machine.
- Perception / aggro / stealth. Every registered enemy is assumed already engaged with the player.
- Multiple enemy archetypes. One melee type. Ranged kiting and shield types would add slot *kinds*, which the
  model allows for but this pass does not build.
- Squad tactics beyond the ring — no flanking manoeuvres, no coordinated pincers.
- Spawning / encounter authoring. Enemies are hand-placed in the level.

## Architecture

Three pieces, in dependency order.

### `USlashEncounterDirector` — a `UWorldSubsystem`

`Source/Slash/Enemy/SlashEncounterDirector.h/.cpp`

A world subsystem rather than an actor: no level placement to forget, auto-created with the world, reachable
from anywhere via `GetWorld()->GetSubsystem<USlashEncounterDirector>()`. It owns:

- **The registry.** `TArray<TWeakObjectPtr<AEnemy>>`, populated in `AEnemy::BeginPlay` and cleared in
  `HandleDeath`. Weak pointers because corpses linger for `CorpseLifeSpan` before destruction, and a corpse
  must stop holding a slot the moment it dies, not three seconds later.
- **The slot ring.** Slot assignments, recomputed on the director's timer.
- **The attack tokens.** Who is currently allowed to commit.

It also caches the player pawn. Everything the director computes is *advice* — it writes a goal onto each
brain and never moves an enemy itself.

### `UEnemyBrainComponent` — per-enemy state machine

`Source/Slash/Enemy/EnemyBrainComponent.h/.cpp`

```
enum class EEnemyBrainState : uint8
{
    Idle,        // no player / not registered
    Circle,      // steering toward assigned slot at standoff radius
    Approach,    // holds a token, closing to attack range
    Attack,      // committed, driving UComboComponent
    Recover,     // post-attack pause, token released, backing off
    Stagger      // hit reaction owns the body; yields to knockback
};
```

Consumes two things written by the director — `SlotGoal` (a world location) and `bHasAttackToken` — and
nothing else. Ticks every frame for steering, which is fine; the *decisions* are all on the director's timer.

### `AEnemyAIController`

`Source/Slash/Enemy/EnemyAIController.h/.cpp`

Exists for NavMesh pathing. `Approach` uses `MoveToLocation` so enemies path around geometry. `Circle` steers
directly with `AddMovementInput` — repathing to a point that orbits a moving player thrashes the navigation
system and produces visible stutter, and there is rarely anything to path around while strafing at a fixed
radius. Falling back to `MoveToLocation` when the direct path is blocked is a Phase 5 refinement, not a
Phase 2 requirement.

## Core data model

### `FCombatSlot`

```
float   Angle;            // radians around the player, world-frame
float   RadiusScale;      // per-slot jitter, ~0.9-1.1, so the ring isn't geometry
TWeakObjectPtr<AEnemy> Occupant;
```

Slots are generated evenly for `NumSlots` (8 is a good default) and only the assignment changes at runtime.
The ring itself is anchored to **world angles, not the player's facing** — anchoring to facing would sweep
every enemy sideways whenever the player turned the camera.

Radius is not a property of the slot, it comes from the occupant's state:

| State | Radius |
|---|---|
| `Circle` | `StandoffRadius` (~400cm) × slot's `RadiusScale` |
| `Approach` / `Attack` | `AttackRadius` (~170cm) |
| `Recover` | `StandoffRadius`, plus a bias away from the player |

### Assignment: stable cyclic matching

The naive version — every enemy takes its nearest free slot — is the thing that makes this system look broken.
Enemies swap slots as the player moves, walking through each other to trade places.

Both enemies and slots lie on a circle, so treat it as a **cyclic matching**:

1. Sort enemies by their current angle around the player.
2. Sort slots by angle.
3. Match in order. An in-order match on a circle is non-crossing by construction, so enemies never trade.
4. There are only `NumSlots` distinct rotational offsets of that matching. Evaluate all of them, take the one
   with the lowest total angular error.

O(N²) with N ≈ 8, i.e. free at 10 Hz. Then apply **hysteresis**: keep the previous assignment unless the new
one beats it by `ReassignMargin`. Without that last step the matching flips between two near-equal offsets and
the whole ring twitches.

Fewer enemies than slots is the normal case and needs no special handling — unoccupied slots simply spread the
occupied ones out. More enemies than slots should raise `NumSlots` rather than double-book.

### Tokens

```
int32 MaxSimultaneousAttackers = 1;   // the single most important tuning value here
float TokenCooldown = 1.2f;           // per-enemy, before the same one may re-take a token
float TokenGrantInterval = 0.6f;      // minimum spacing between grants, so attacks don't sync up
```

Grant priority goes to whoever is closest to attack range and roughly facing the player, with the per-enemy
cooldown preventing one aggressive enemy from monopolising the fight.

Tokens must be released on **stagger** and on **death**, not only on attack completion. An enemy that holds a
token while flinching is an attack slot spent on nothing, and it reads to the player as the crowd going
passive for no reason.

## Phases

Each phase is independently playable. Do not start the next one until the current one looks right in PIE —
this is a feel system, and stacking three unverified layers makes the eventual bug unfindable.

### Phase 0 — Locomotion

Enemies move at all. No positioning logic.

- `NavMeshBoundsVolume` in the level, covering the play space. Verify with `P` in the viewport.
- `AEnemyAIController`; set `AIControllerClass` and `AutoPossessAI = PlacedInWorldOrSpawned` on `AEnemy`.
- Naive chase: `MoveToLocation(Player, AcceptanceRadius = AttackRadius)` on a repeating timer.
- Enable actor tick, or keep `AEnemy` tickless and let the brain component tick.

**Done when:** enemies walk to the player, stop at range, and path around an obstacle. Locomotion animation
should work for free — enemies already share `ABP_SlashCharacter`, which drives from velocity.

**Watch for:** knockback. `AEnemy` applies `LaunchCharacter` on hit, and an active `MoveTo` will fight it.
Phase 0 can live with it; Phase 3's `Stagger` state fixes it properly by yielding for the reaction's duration.

### Phase 1 — Director skeleton and registry

Plumbing only, no behaviour change.

- `USlashEncounterDirector` with register/unregister, player caching, and the 10 Hz timer.
- `AEnemy::BeginPlay` registers; `HandleDeath` unregisters and releases anything held.
- `slash.Encounter.Debug` cvar: draw the ring, every registered enemy, and a line to whatever it's assigned.

**Done when:** the debug draw shows the right enemy count, and killing one removes it immediately rather than
when the corpse despawns.

The debug draw is not optional polish — it is the only way to tune Phases 2 and 3. Build it here, before it is
needed, following the `slash.MeleeTrace.Debug` pattern in `UMeleeWeaponComponent`.

### Phase 2 — The ring

- `FCombatSlot` generation, the cyclic matching, hysteresis.
- Director writes `SlotGoal` onto each brain; brains steer to it at `StandoffRadius`.
- `Circle` and `Idle` states only. Nobody attacks yet.

**Done when:** six enemies spread evenly around the player and hold position as the player strafes and turns.
Specifically check that rotating the camera does not move a single enemy, and that walking a lap around the
group does not make anyone swap places.

### Phase 3 — Turn-taking

- Token grant/release on the director; `MaxSimultaneousAttackers`, cooldowns, grant spacing.
- Brain gains `Approach`, `Recover`, `Stagger`. Still no actual attack — the token holder closes to
  `AttackRadius`, pauses, then releases and returns to the ring.
- `AEnemy::TakeDamage` drives `Stagger`, which releases the token and yields to the knockback.

**Done when:** with six enemies, one closes in at a time while the rest visibly circle, and hitting the
committed enemy sends someone else in. This is the phase that determines whether the fight reads well; expect
to spend real time on the three tuning values before moving on.

### Phase 4 — Enemy attacks

The payoff for how the combat system was built: `UComboComponent` is an actor component that binds its own
montage notifies and works on any `ACharacter`, and `UMeleeWeaponComponent` detects its own hits. Enemy
attacks are mostly content.

- Add `UComboComponent` + `UMeleeWeaponComponent` to `AEnemy`, and a `DA_Combo_EnemySword` tree asset.
- Brain's `Attack` state calls `ComboComponent->RequestAttack(...)` and holds the token until `OnComboEnded`.
- **Prerequisite:** `ASlashCharacter` has no `UHealthComponent`. It needs one before an enemy swing can mean
  anything. That is a small change but it is a real blocker on this phase, not an afterthought.
- The enemy's blade needs `BladeBaseSocket`/`BladeTipSocket` on whatever mesh it swings, or
  `UMeleeWeaponComponent` disables itself in `BeginPlay` and logs an error.

**Done when:** a token-holding enemy swings, connects, and damages the player, with the hit window opening and
closing off the same `AttackHit` notify the player uses.

### Phase 5 — Feel

Ordered roughly by value.

- `bUseRVOAvoidance` on the enemy movement component. Try this **before** writing custom separation steering —
  it is free and usually sufficient once the ring is already doing the coarse spacing.
- Per-slot `RadiusScale` jitter and slight per-enemy speed variance, so the ring stops reading as geometry.
- A telegraph beat: hold at `AttackRadius` for a wind-up before committing, so the player can react.
- `Recover` backing off along the slot's outward normal rather than teleporting back to the ring.
- Re-check `ASlashCharacter::LeapTowardTarget`. It fixes both leap endpoints at leap start, which was correct
  when enemies were stationary; against a circling target the swing may now land behind them. The existing
  comment says this was deliberate against knockback — it needs revisiting, not reverting.

## Camera-aware placement (the God of War layer)

Added 2026-09-27 on top of Phases 0-4, modelled on how God of War (2018) keeps a crowd readable under a tight
over-the-shoulder camera. It changes the ring above in three ways.

- **A camera-facing arc, not a world-anchored ring.** Slots are spread along an arc centred on an anchor yaw
  that follows the camera. Half-width starts at `MinArcHalfDegrees` (35) and grows by `PerEnemyArcDegrees`
  (22) per extra enemy, closing into a full ring around eight. The ring's original worry — turning the camera
  sweeps every enemy sideways — is handled by the anchor rather than by world anchoring: it ignores the camera
  inside `AnchorDeadZoneDegrees` (40), and once outside it turns at most `AnchorTurnRateDegrees` (70/s) until
  caught up. A glance moves nobody; turning away for real makes the crowd *orbit* back into view.
- **Orbiting, not crossing.** On an arc the in-order matching needs no rotational search, but an enemy directly
  behind sits on the seam and would flip ends every tick; `BehindSide` pins it. Brains steer toward a point at
  most `MaxOrbitStepDegrees` (35) further round the player, so reaching the far side traces an arc around them.
- **Off-screen rules.** The director marks each enemy on/off-screen (camera FOV × `OnScreenFOVFraction`).
  Off-screen enemies stand `OffscreenStandoffScale` (1.25x) further out, tokens go to on-screen enemies, and an
  off-screen one only gets a token after `OffscreenAttackDelay` (3s) with no on-screen candidate — then it
  telegraphs for `OffscreenWindupTime` (1.0s vs 0.4s). The HUD draws an edge indicator per off-screen enemy
  within 18m: dim white while circling, orange closing in, building to red through the wind-up.

Waves also spawn out of view: `ASlashGameMode` scores every spawn point (placed enemies plus `EnemySpawn`-tagged
TargetPoints around the arena edge) to prefer ones at least `MinSpawnDistance` away and outside the view cone.

## Tuning surface

Everything lives on the director as `EditDefaultsOnly`, because these are encounter-level values, not
per-enemy ones:

```
int32 NumSlots = 8;
float StandoffRadius = 400.f;
float AttackRadius = 170.f;
float ReassignMargin = 0.35f;          // radians the new matching must beat the old by
int32 MaxSimultaneousAttackers = 1;
float TokenCooldown = 1.2f;
float TokenGrantInterval = 0.6f;
float DirectorTickRate = 10.f;         // Hz
```

## Why not a Behaviour Tree

The director makes every interesting decision — which slot, who may attack — and both are *global* choices
across all enemies, which a per-agent tree cannot express. What is left for the enemy is a handful of states
driven by external signals, which is a state machine.

A BT here would mean a BehaviorTree asset, a Blackboard asset, and custom task/service/decorator classes
wrapping C++ that would exist anyway. It also moves iteration into the editor: BT and Blackboard graphs are
hand-authored, so every tweak becomes a click-through instead of a ten-second rebuild.

This becomes worth revisiting with several genuinely different archetypes — a ranged type that kites, a shield
type that repositions, a leader. The split above survives that change: keep the director in C++ and let the
brain become a BT (or a StateTree, which fits this shape better) reading `SlotGoal` and `bHasAttackToken` off
a blackboard the director writes. Nothing about the ring or the tokens changes.

## Open questions

- **Ring anchoring during lock-on.** `USlashTargetingComponent` gives the player a locked target. Should the
  locked enemy get priority for a token, or should it be *denied* one so the player's chosen target isn't also
  the one hitting them? Worth trying both in Phase 3.
- **Slot count vs. arena size.** Eight slots at 400cm needs roughly a 9m clearing. Tight spaces need the ring
  to collapse or become an arc, which is Phase 5 work at the earliest.
- **Registry vs. targeting.** `USlashTargetingComponent` finds candidates with an overlap sweep every time.
  Once the director holds a registry, lock-on could read from it instead — cheaper and consistent. Deliberately
  not folded into this plan; it is a targeting change, not a positioning one.

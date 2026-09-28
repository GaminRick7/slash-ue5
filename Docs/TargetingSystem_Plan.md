# Enemy Selection & Lock-On — Design Plan

Status: **design only, not implemented**. No code has been written.

> **Partially superseded by `TargetingSystem_Spec.md` (2026-08-01).** A dedicated Targetable
> collision channel became available, which collapses the targetable contract: there is **no
> `ISlashTargetable` interface**, and `UTargetableComponent` is a `USphereComponent` sitting on that
> channel rather than a bare `UActorComponent` — so the aim point is the sphere's own location and
> `AimSocketName` no longer exists. That invalidates **§1**, **§5**, the `SlashTargetable.h` row of
> §7's file table, and trap #3 in §9. Everything else — the two-query model, the scorer, stickiness
> and hysteresis, the whole of hard lock-on and the `Look()` ordering trap — is unchanged and still
> the reasoning of record. Read the spec for what to build; read this for why.

Starting point, verified 2026-08-01: the project has **nothing targetable in it**. There is no
enemy class, no AI, no health, no hit detection, and `UComboComponent::OnAttackHitWindow` is
declared but never fires (no `AttackHit` notify exists in any montage). So this plan has to define
what an "enemy" *is* before it can define how one is picked. It does that with the smallest
contract that works — an interface plus a component plus a test dummy — and deliberately does not
build enemies, damage, or AI.

Two decisions were made up front and everything below follows from them:

- **Hard lock (Souls-like).** The lock-on override drives the *camera*, not just selection. Control
  rotation is interpolated toward the target, player look becomes a bounded offset on top, and the
  character strafes around the target instead of squaring up to the camera.
- **Ship a test dummy.** `ISlashTargetable` + `UTargetableComponent` + `BP_TargetDummy`, so the
  whole system is PIE-testable on day one without waiting for real enemies.

## Goals

- **"Best enemy" is answered twice, not once.** Attack-time soft targeting (which enemy does *this
  swing* go at) and persistent targeting (which enemy is the reticle/lock candidate) are different
  questions with different weights. One scoring function, two weight sets.
- **Reusable and decoupled**, matching `UComboComponent` and `USlashCameraRigComponent`: an
  `UActorComponent` that never reaches back into the character, broadcasts delegates outward, and
  binds no input itself.
- **No reference to a concrete enemy type.** The selection component only ever sees
  `ISlashTargetable`. A rock, a training dummy, and a future `AEnemyCharacter` are all the same to
  it.
- **Cheap.** Full candidate rescan at ~10 Hz, not per frame; cheap filters before any line trace;
  a hard cap on how many candidates get traced.
- **Stable.** The incumbent target gets a stickiness bonus so two near-equal candidates don't
  flicker. This is the term naive implementations skip, and it is the single most visible failure
  mode.

## Non-goals (for this pass)

- Damage, health, hit detection, hit reactions. `OnAttackHitWindow` stays inert; wiring it is a
  separate pass (see [Future work](#future-work)).
- Enemy AI of any kind. `BP_TargetDummy` does not move or fight.
- Reticle / lock-on marker UI. There is no HUD or UMG in the project yet. The plan exposes
  `OnTargetChanged` so a widget can bind later, and ships a debug-draw cvar instead.
- Homing / target-tracked root motion. Facing is snapped toward the target on attack entry; the
  dash itself stays the existing `LaunchCharacter(GetActorForwardVector() * DashSpeed)`.
- Multiplayer / replication. Everything here is local-player-authoritative, same as the rest of the
  project.

---

## 1. The targetable contract

### `ISlashTargetable` (`SlashTargetable.h`)

A `UINTERFACE` with `BlueprintNativeEvent` functions so Blueprint-only actors can implement it
directly without the component.

```
bool IsTargetable() const;              // false while dead, ragdolled, spawning, phased out
FVector GetTargetPoint() const;         // world-space aim point
float GetTargetPriorityBias() const;    // additive score nudge, default 0
```

`GetTargetPoint()` is **not** the capsule centre. It resolves a socket (chest height) if the actor
has one, and falls back to actor location plus a Z offset. Getting this wrong is the classic
lock-on bug: the camera frames the enemy's feet and the reticle sits on the floor.

`GetTargetPriorityBias()` is the escape hatch for "this boss should always win over the trash
around it" without adding a weight term to the scorer.

### `UTargetableComponent` (`TargetableComponent.h/.cpp`)

`UActorComponent` implementing `ISlashTargetable`. Drop it on any actor and it becomes targetable.

```
bool bTargetable = true;                        // runtime gate; set false on death
FName AimSocketName = NAME_None;                // resolved against the owner's first SkeletalMeshComponent
FVector AimOffset = FVector(0, 0, 60);          // used when no socket, or added on top of one
float TargetPriorityBias = 0.f;
float LockOnMaxDistanceOverride = 0.f;          // 0 = use the targeting component's default
```

Also exposes `SetTargetable(bool)` so a future death handler has one call to make.

### `BP_TargetDummy` (content)

`/Game/Combat/BP_TargetDummy` — an `AActor` with a capsule or static mesh, a collision setup that
lands it in the targeting query (see [§5](#5-collision-and-the-query-channel)), and a
`UTargetableComponent`. Enough to place four of them in a test level and verify selection, lock-on,
cycling, and lock-break by walking away.

---

## 2. `USlashTargetingComponent`

`UActorComponent` on `ASlashCharacter`, alongside `ComboComponent` and `CameraRig`.

**It does not tick itself.** The character calls `UpdateTargeting(DeltaSeconds)` at the top of its
`Tick`, before `UpdateFacing`. This is the same discipline `USlashCameraRigComponent` uses and for
the same reason — update order stays explicit, and the lock-on control rotation must be written
before the spring arm reads it.

### Scoring

One function, called with a weight set:

```
FSlashTargetWeights
    float MaxDistance;
    float MinDot;              // cone half-angle as a dot product against the intent direction
    float DistanceWeight;
    float AngleWeight;
    float StickinessBonus;     // added to the incumbent target's score only
    bool  bRequireLineOfSight;
```

```
Score(Candidate, IntentOrigin, IntentDir, Incumbent):
    Dist      = |TargetPoint - IntentOrigin|
    DirTo     = normalize(TargetPoint - IntentOrigin)
    Dot       = IntentDir · DirTo

    // hard filters, cheapest first
    reject if !ISlashTargetable::IsTargetable()
    reject if Dist > MaxDistance
    reject if Dot  < MinDot

    DistScore  = 1 - clamp(Dist / MaxDistance, 0, 1)
    AngleScore = remap(Dot, MinDot..1, 0..1)

    Score = AngleWeight * AngleScore
          + DistanceWeight * DistScore
          + GetTargetPriorityBias()
          + (Candidate == Incumbent ? StickinessBonus : 0)
```

Line-of-sight is checked **last and only on the top N candidates by score** (`MaxTracedCandidates`,
default 5): sort, then walk the sorted list tracing until one passes. A single `LineTraceSingleByChannel`
on `ECC_Visibility` from `IntentOrigin` to the target point, ignoring self and the candidate.

### The two weight sets

|                       | `SoftTargetWeights` (attack-time)     | `LockTargetWeights` (persistent) |
|-----------------------|---------------------------------------|----------------------------------|
| Intent origin         | Character location                    | Camera location                  |
| Intent direction      | **Stick direction**, camera forward as fallback | Camera forward           |
| `MaxDistance`         | 600                                   | 2000                             |
| `MinDot`              | -0.17 (≈100° half-angle — generous)   | 0.34 (≈70° half-angle)           |
| `AngleWeight`         | 0.75                                  | 0.55                             |
| `DistanceWeight`      | 0.25                                  | 0.45                             |
| `StickinessBonus`     | 0 — no hysteresis, it's a one-shot    | 0.15                             |
| `bRequireLineOfSight` | false — a swing at someone behind a railing is fine | true               |

**Soft targeting is only consulted in the unlocked path.** When locked, §3d uses the lock target
directly and never calls `FindSoftTarget`. This is not just a priority choice — `CachedMoveDirection`
is camera-relative, and under hard lock the camera points at the target, so "stick forward" would
mean "toward the current target" rather than anything the player aimed. Soft targeting's stick
intent is only meaningful when the player controls the camera.

The stick-direction intent for soft targeting is the whole reason the swing feels intentional:
point the stick at the enemy on your left, swing, and the character turns and dashes at *that* one
even though the camera is pointed elsewhere. `ASlashCharacter` already caches exactly what this
needs — `CachedMoveDirection` and `MoveInputFrame` (`SlashCharacter.h:180-186`), whose comment
already says it exists because combos suppress `AddMovementInput` and "the character still needs to
know where the player is pointing." Freshness test is the same `(GFrameCounter - MoveInputFrame) <= 1`
check `UpdateFacing` uses.

### Persistent target refresh

Rescan on an accumulator inside `UpdateTargeting` (`RescanInterval = 0.1s`), not every frame.
Between rescans the cached target is reused. Rescanning at 10 Hz is invisible for a reticle and
cuts the overlap query and traces by ~6×.

### Lock-on state and break conditions

While locked, the lock target is **not** re-scored — it is only *validated*, every frame:

- target destroyed or `!IsTargetable()` → break immediately
- distance > `LockBreakDistance` (default 2400, deliberately larger than `LockTargetWeights.MaxDistance`
  so acquisition and retention have hysteresis) → break
- line of sight blocked continuously for longer than `LockLoseSightGrace` (default 0.6s) → break.
  A grace period matters: pillars and the enemy's own animation break LOS constantly, and breaking
  lock on a single failed trace is maddening.
- player presses `IA_LockOn` again → break

Every break routes through one `ClearLockOn(EReason)` so the camera hand-back is identical in all
cases.

**Breaking mid-combo** is left as-is deliberately. `UpdateFacing` falls back to the
`bIsInCombo ? stick : control` ternary on the next frame, and with no fresh stick input that hits
the early return — so facing freezes where it was rather than snapping to the camera. That is the
right behaviour (a target dying mid-swing shouldn't whip the character around), and it's recorded
here so it isn't later "fixed" as a bug. `ClearLockOn` also zeroes the look offsets, so control
rotation is handed back clean.

### Target cycling

Standard flick-to-switch, since hard lock without it is unusable with more than two enemies. While
locked, a `Look` input whose absolute yaw magnitude exceeds `CycleFlickThreshold` (and only after
`CycleCooldown`, default 0.35s, has elapsed) calls `CycleTarget(Sign)` instead of feeding the look
offset. Candidates are projected onto the camera's right vector and the nearest one on the flicked
side of the current target wins.

### Public API

```
void UpdateTargeting(float DeltaSeconds);                  // called from ASlashCharacter::Tick

AActor* FindSoftTarget(const FVector& IntentDirection) const;   // one-shot, no state, no hysteresis
AActor* GetPersistentTarget() const;                            // 10Hz-refreshed best candidate
AActor* GetActiveTarget() const;                                // lock target if locked, else persistent
bool    IsLockedOn() const;

void ToggleLockOn();
void SetLockOn(bool bEnabled);
void CycleTarget(float DirectionSign);

bool GetTargetPoint(AActor* Target, FVector& OutPoint) const;   // interface call + null guard, in one place
```

### Delegates

Same shape as `UComboComponent`'s, so binding follows the pattern already in `ASlashCharacter::BeginPlay`.

```
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTargetChanged, AActor*, NewTarget, AActor*, OldTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLockOnChanged, bool, bLockedOn, AActor*, Target);

FOnTargetChanged OnTargetChanged;   // persistent target changed — future reticle binds here
FOnLockOnChanged OnLockOnChanged;   // lock acquired or broken
```

---

## 3. Character integration

### 3a. `UpdateFacing` must be restructured, not extended

`ASlashCharacter::UpdateFacing` (`SlashCharacter.cpp:309`) currently early-returns when there is no
fresh move input:

```cpp
const bool bHasFreshInput = (GFrameCounter - MoveInputFrame) <= 1 && !CachedMoveDirection.IsNearlyZero();
if (!bHasFreshInput) { return; }
```

Under lock-on the character must keep facing the target **while standing still**, so the lock-on
branch goes *before* that return — it cannot be a third arm of the existing
`bIsInCombo ? stick : control` ternary. New shape:

1. If locked on and the target resolves → target rotation is yaw toward the target point, interp at
   `LockFacingInterpSpeed` (default 12). Return.
2. Otherwise the existing fresh-input gate and ternary, unchanged.

### 3b. Hard lock drives the **controller**, not the boom

`CameraBoom->bUsePawnControlRotation = true` (`SlashCharacter.cpp:49`), so the boom takes its
rotation straight from the control rotation. Rotating the boom directly would be fought and
overwritten. Hard lock therefore interpolates `Controller->SetControlRotation(...)`, in the
character's `Tick` before `CameraRig->UpdateRig(...)`.

Desired rotation is `(TargetPoint - BoomOrigin).Rotation()`, plus a small downward pitch bias so the
target sits slightly above screen centre rather than dead centre, plus the player's look offset:

```
Desired = (TargetPoint - BoomWorldOrigin).Rotation()
Desired.Pitch += LockPitchBias                    // default -8°, framing
Desired += FRotator(LookOffsetPitch, LookOffsetYaw, 0)
ControlRotation = RInterpTo(ControlRotation, Desired, Dt, LockCameraInterpSpeed)   // default 8
```

**`Look()` policy while locked** — the contention this creates has to be decided explicitly, and
the decision is: look input becomes a *bounded, decaying offset*, not a rotation.

This requires changing `Look()` itself, and the reason is a real ordering trap. `Look()`
(`SlashCharacter.cpp:188`) currently calls `AddControllerYawInput` / `AddControllerPitchInput`,
which accumulate into the controller's `RotationInput`. `APlayerController::UpdateRotation` consumes
that and writes control rotation **after** the character's `Tick` — so a `SetControlRotation` made
during `Tick` would simply be overwritten by the player's own stick input the same frame, every
frame. The lock would feel like it was fighting the player, which is precisely the failure the rest
of this section exists to avoid.

So, while locked, `Look()` must accumulate into `LookOffsetYaw` / `LookOffsetPitch` and **return
without calling the `AddController*Input` functions at all**:

```cpp
void ASlashCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D LookAxisVector = Value.Get<FVector2D>();
    if (!Controller) { return; }

    if (TargetingComponent && TargetingComponent->IsLockedOn())
    {
        TargetingComponent->AddLookInput(LookAxisVector);   // offset, or a cycle flick
        return;                                             // no AddController*Input — see above
    }

    AddControllerYawInput(LookAxisVector.X);
    AddControllerPitchInput(LookAxisVector.Y);
}
```

The offsets are each clamped (`LockLookOffsetYawMax` 25°, `LockLookOffsetPitchMax` 20°) and decay
back to zero at `LockLookOffsetDecaySpeed` (default 4). Large yaw flicks are intercepted by target
cycling first (§2), which is why `AddLookInput` lives on the targeting component rather than the
character. This keeps the stick feeling live without ever letting the player fight the lock — the
same reasoning that turned rotation lag off on this rig (see `Docs/PolishPass.md`).

### 3c. Strafe locomotion is nearly free

`bOrientRotationToMovement` is already false and facing is already hand-rolled, so locking facing to
the target automatically produces strafing movement — and `ABP_SlashCharacter`'s `Direction`
variable and `BS_Jog2D` blendspace already exist to render it. **No AnimBP work is required for
strafing**, which matters because anim state machines have to be authored by hand. A dedicated locked-on walk set is optional
polish, not a dependency.

**Sprint interaction:** `UpdateSprintState` (`SlashCharacter.cpp:286`) gates on
`SprintForwardDotThreshold` (0.7) between `CachedMoveDirection` and **camera forward** — not the
character's facing. Under hard lock the camera points at the target, so the two coincide and the
rule falls out correctly with no change: you can sprint *at* the target, and strafing sideways or
backpedalling stays a jog. That is the intended Souls-like behaviour. Recommendation: change
nothing here, confirm it in PIE, and only then consider a "sprint breaks lock" rule. Note the
coupling though — because the gate is camera-relative, the bounded look offset from §3b shifts the
sprint cone slightly with it. At a 25° max offset against a 0.7 dot (≈45°) that is harmless, but
raising `LockLookOffsetYawMax` much past that starts eating the sprint cone.

### 3d. The payoff: facing snap on combo node entry

This is what makes selection actually matter. `HandleComboNodeEntered` (`SlashCharacter.cpp:374`)
currently ends with:

```cpp
SetRootMotionMovementActive(Node->bRootMotionMovement);

if (!Node->bRootMotionMovement && Node->DashSpeed != 0.f)
{
    LaunchCharacter(GetActorForwardVector() * Node->DashSpeed, true, false);
}
```

Insert the snap immediately after the `if (!Node) { return; }` guard — i.e. **above**
`SetRootMotionMovementActive`, so it runs for root-motion and dash nodes alike. Resolve a target and
snap yaw toward it:

- If locked on → the lock target.
- Otherwise → `TargetingComponent->FindSoftTarget(IntentDir)`, where `IntentDir` is
  `CachedMoveDirection` if fresh, else camera forward.
- If nothing is found → unchanged behaviour, current facing.

Snap (not interp) the yaw, then let `LaunchCharacter` read the already-corrected
`GetActorForwardVector()`. Clamp the snap to `MaxAttackFacingSnap` (default 60°) so an attack can't
teleport the character's facing across the screen.

**This matters most on the aerial nodes, not least.** Root-motion nodes are excluded from the
`LaunchCharacter` branch on purpose — `LaunchCharacter` only queues `PendingLaunchVelocity`, and
`HandlePendingLaunch` forces `MOVE_Falling` on the next `PerformMovement`, silently undoing
`MOVE_Flying` a frame later. So for aerials the facing snap is the *only* steering the player gets,
and the montage's root motion carries the character along whatever direction the snap chose. Putting
the snap inside the dash branch would silently skip every aerial node.

### 3e. Camera rig gets a fifth layer

Follow the existing push-in pattern exactly — nothing new is needed:

- Add `bool bLockedOn` to `FSlashCameraRigInput`.
- Add `FSlashCameraProfile LockOnProfile` beside Default/Sprint/Combat/Air, and a `LockAlpha`
  alongside `AirAlpha`/`CombatAlpha`.
- Suggested profile: slightly longer arm than Combat (~300) and a wider FOV (~88) so two fighters
  fit in frame, with a small negative `SocketOffset.Y` shift toward centre — over-the-shoulder
  framing plus a locked target reads as lopsided.

Because profiles are blended as layers rather than as an exclusive state machine, a locked-on
sprinting player in the air still reads as a mix. No priority arbitration to write.

---

## 4. Input

One new action, `IA_LockOn` — **already created** at `/Game/Input/IA_LockOn` (Boolean, no triggers).
Bound in `ASlashCharacter::SetupPlayerInputComponent` on `ETriggerEvent::Started` →
`TargetingComponent->ToggleLockOn()`. Same optional-asset guard pattern as `JumpAction`
(`if (LockOnAction) { ... }`), so the system stays inert until the asset is assigned.

The `IMC_Gamepad` binding is still to be added by hand, and the key needs choosing rather than
assuming — right-stick click is the conventional choice, but `Gamepad_RightThumbstick` already
appears in the mapping list under `IA_Look`. See §8.

Following the combo system's precedent, the targeting component **binds no input itself** — the
character forwards. That keeps it usable by AI actors later.

---

## 5. Collision and the query channel

Candidate gathering is one `UWorld::OverlapMultiByObjectType` call (not the
`UKismetSystemLibrary::SphereOverlapActors` wrapper — the raw call avoids the actor-array allocation
and lets the object-type set be built once as a member) with a sphere of
`LockTargetWeights.MaxDistance` centred on the character.

Recommendation: query `ECC_Pawn` **plus** `ECC_WorldDynamic` and filter by "does the actor implement
`ISlashTargetable`", rather than adding a custom object channel. Reason: a custom channel means a
`DefaultEngine.ini` edit and remembering to set it on every future enemy — one more thing to get
silently wrong — and the interface check is the real filter anyway. If profiling ever shows the
overlap is too broad, a dedicated `Targetable` trace channel is the escape hatch, and the plan's
structure doesn't change.

`BP_TargetDummy` therefore just needs a collision component that responds to overlap queries on one
of those object types.

---

## 6. Debug

`slash.Targeting.Debug 1` (an `FAutoConsoleVariable`) draws, each rescan:

- a sphere at every candidate's `GetTargetPoint()`, coloured by pass/reject
- the numeric score beside each
- the intent direction as a line
- the active lock target in a distinct colour, with its LOS trace

`DrawDebugHelpers.h` is in `Engine`, so this needs no new module.

Alongside it, `UE_LOG` on lock acquire/break with the reason — per the project's debugging
convention, log lines in the output log show what happened during a PIE session.

---

## 7. Files

| File | New/Changed |
|---|---|
| `Source/Slash/SlashTargetable.h` | new — `UINTERFACE` + `ISlashTargetable` |
| `Source/Slash/TargetableComponent.h/.cpp` | new — default implementation |
| `Source/Slash/SlashTargetingComponent.h/.cpp` | new — the selection + lock-on component |
| `Source/Slash/SlashCharacter.h/.cpp` | changed — component, `IA_LockOn`, `UpdateFacing` restructure, control-rotation drive, `HandleComboNodeEntered` snap |
| `Source/Slash/SlashCameraRigComponent.h/.cpp` | changed — `bLockedOn` input field, `LockOnProfile`, `LockAlpha` |
| `Source/Slash/Slash.Build.cs` | **unchanged** |

`Build.cs` staying untouched is deliberate and worth protecting: a `Build.cs` edit cannot be applied
by Live Coding and turns each iteration into an editor restart plus a full rebuild. Everything this
plan needs (`Engine`, `EnhancedInput`, `DrawDebugHelpers`) is already linked.

---

## 8. Editor handoff

### Already done

**`IA_LockOn` exists.** Created and saved at `/Game/Input/IA_LockOn` on 2026-08-01, `ValueType`
`Boolean`, no triggers — which is what `ETriggerEvent::Started` wants.

### Still manual

1. **Add the mapping** in `IMC_Gamepad`. A plain mapping with no trigger is all that's
   needed.
   **Pick the key carefully:** the live asset shows `Gamepad_RightThumbstick` already bound to
   `IA_Look`, so the suggestion earlier in this plan (§4) collides with it. Confirm in-editor which
   key is the right-stick *click* on this project's setup before binding, rather than assuming.
2. **Assign `LockOnAction`** on `BP_SlashCharacter`'s defaults.
3. **Build `BP_TargetDummy`** at `/Game/Combat/BP_TargetDummy` and place ~4 in the test level at
   varied distances and angles, including one behind cover to exercise the LOS path.
4. **Set the aim socket** on each dummy's `UTargetableComponent` (or leave `AimSocketName` empty and
   rely on `AimOffset` for a static mesh).

## 9. Traps, recorded before they're hit

1. **`UpdateFacing`'s early return.** Restructure, don't append — §3a. Appending is the intuitive
   move and it produces a lock-on where the character faces the target only while you're moving.
2. **The boom takes control rotation.** Drive the controller, not the boom — §3b.
3. **Aim point isn't capsule centre.** Chest socket or Z offset, or the camera frames the floor.
4. **Facing snap goes above the `bRootMotionMovement` branch** in `HandleComboNodeEntered`, not
   inside it — otherwise every aerial node silently loses its only steering. §3d.
5. **Ordering inside `Tick`.** `UpdateTargeting` → control rotation → `UpdateFacing` →
   `UpdateRig`. The rig must see the control rotation the lock already wrote this frame, or the
   camera lags a frame behind the lock permanently. Note the write lands on the *`Controller`*, and
   `APlayerController::UpdateRotation` runs after `Tick` and would clobber it from accumulated
   `RotationInput` — the `Look()` suppression in §3b is what makes the write stick. These two are a
   pair; neither works alone.
6. **Stickiness is not optional.** Without `StickinessBonus`, two enemies at similar range and angle
   swap the reticle every rescan.
7. **LOS grace period is not optional.** A single blocked trace must not break lock.
8. **Don't rescan the lock target.** Once locked, validate only. Re-scoring means the lock silently
   slides to whatever is closest, which is not what the player asked for.

---

## 10. Suggested build order

Each phase is independently testable, so nothing is integrated blind.

1. **Contract + dummy.** `ISlashTargetable`, `UTargetableComponent`, `BP_TargetDummy`. Test: place
   dummies, confirm the interface resolves.
2. **Selection + debug draw.** `USlashTargetingComponent` with candidate gathering, scoring, the
   10 Hz rescan, and `slash.Targeting.Debug`. No behaviour change to the character yet — this is
   verified entirely by looking at the debug spheres and scores while walking around.
3. **Soft targeting into combat.** The `HandleComboNodeEntered` facing snap. First point at which
   the system changes how the game plays, and the highest feel-per-line change in the plan.
4. **Lock-on.** `IA_LockOn`, `UpdateFacing` restructure, control-rotation drive, look offset, break
   conditions.
5. **Camera + cycling.** `LockOnProfile` layer, flick-to-cycle, framing pitch bias.

Phases 1–3 are useful on their own even if lock-on is deferred — soft targeting is what makes the
existing combo dash feel aimed, and it needs no camera work at all.

---

## Future work

- **Wire `OnAttackHitWindow`.** It has been declared and inert since the combo system landed. An
  `AttackHit` notify in the montages plus a sweep against the same `ISlashTargetable` actors closes
  the loop from "picked a target" to "hit it."
- Reticle / lock marker UMG widget bound to `OnTargetChanged`.
- `AEnemyCharacter` base with health, driving `UTargetableComponent::SetTargetable(false)` on death
  so lock breaks for free.
- Score term for screen-space position rather than world angle, once there's a HUD to reason about.
- Per-node soft-target overrides on `FComboNode` (a wide sweeping finisher wants a wider cone than a
  thrust).
- Assist strength on the dash: scale `DashSpeed` toward the exact distance to the target so combos
  close the gap rather than over- or under-shooting.

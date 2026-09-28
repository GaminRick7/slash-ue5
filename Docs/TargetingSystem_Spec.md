# Targeting System — Class Spec by Phase

Status: **spec only, not implemented.** Companion to `TargetingSystem_Plan.md`, which holds the
reasoning. This holds the classes, members and signatures.

## How this differs from the plan

The plan proposed an `ISlashTargetable` interface plus a plain `UActorComponent`, with candidates
gathered by overlapping `ECC_Pawn`/`ECC_WorldDynamic` and filtered by interface check. **That is
superseded.** With a dedicated Targetable collision channel available, the design collapses:

- **No interface.** The component is the only implementer, and the physics query hands it to you
  directly — so the `UINTERFACE` pair, `Execute_` dispatch and the dual-shape `Resolve()` all go.
- **`UTargetableComponent` is a `USphereComponent`**, not a bare `UActorComponent`. It *is* the
  collision volume on the Targetable channel.
- **The aim point is the component's own location**, so `AimSocketName` / `AimOffset` /
  `DoesSocketExist` / the missing-socket warning are all deleted. Place the sphere at chest height
  in the viewport; attach it to a skeletal socket in the editor if it should track animation.
- **No broad-phase filtering.** The channel means the query returns only targets.

**Assumption to confirm:** the Targetable channel is an **Object** channel, so gathering is
`OverlapMultiByObjectType`. If it is a **Trace** channel instead, two things change together: the
query becomes `OverlapMultiByChannel`, **and** the constructor's blanket
`SetCollisionResponseToAllChannels(ECR_Ignore)` must be followed by `ECR_Overlap` on the Targetable
channel — otherwise the query matches nothing. Those two are a pair; changing only the call site
produces a system that silently finds zero candidates.

### The channel constant

There is no `ECC_Targetable` symbol. Custom channels declared in `DefaultEngine.ini` are only ever
`ECC_GameTraceChannel1`…`ECC_GameTraceChannel18` in C++ — the friendly name exists in the ini and
the editor UI, not in code. Add the alias once, in `Source/Slash/Slash.h`, and use it everywhere:

```cpp
// Must match the Targetable channel's slot in DefaultEngine.ini.
#define COLLISION_TARGETABLE ECC_GameTraceChannel1
```

Confirm the actual slot number in `DefaultEngine.ini` before writing code — if Targetable isn't the
first custom channel, the number differs and the mismatch is silent (you'd query a channel nothing
is on and get zero candidates).

---

## Phase 1 — The contract

### `UTargetableComponent : USphereComponent`

`Source/Slash/TargetableComponent.h/.cpp`

Being on an actor is what makes it targetable. Holds the aim point and the runtime gate, nothing
else — scoring, hysteresis and lock state all live on the targeting side.

```cpp
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SLASH_API UTargetableComponent : public USphereComponent
{
public:
    UTargetableComponent();

    // Aim point. Not the capsule centre — a target point at the feet makes the camera frame the
    // floor and puts any future reticle on the ground.
    //
    // Deliberately a plain virtual, not a BlueprintNativeEvent: the sphere's placement in the
    // viewport already *is* the aim point, so a BP override buys nothing and would drag back the
    // Execute_GetTargetPoint(Comp) ceremony this design exists to remove. A C++ subclass can still
    // override it. Revisit only if a target needs an aim point BP can compute but the transform
    // can't express.
    virtual FVector GetTargetPoint() const;                  // returns GetComponentLocation()

    UFUNCTION(BlueprintCallable, Category = "Targeting")
    void SetTargetable(bool bNewTargetable);

    UFUNCTION(BlueprintPure, Category = "Targeting")
    bool IsTargetable() const;                               // bTargetable && IsValid(GetOwner())

    // Sphere radius, for a future "big enemies are easier to acquire" score term.
    UFUNCTION(BlueprintPure, Category = "Targeting")
    float GetTargetRadius() const;                           // GetScaledSphereRadius()

protected:
    // Runtime gate. Authoritative; SetTargetable mirrors it into the collision response so a dead
    // enemy is rejected at broad-phase, before any scoring.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting")
    bool bTargetable = true;

    // Additive score nudge, so a boss outranks the trash around it without the scorer needing a
    // term for it.
    UPROPERTY(EditAnywhere, Category = "Targeting")
    float TargetPriorityBias = 0.f;

    // 0 = inherit the targeting component's range. NARROWS ONLY — it cannot extend range, because
    // GatherCandidates sweeps at LockTargetWeights.MaxDistance and a target outside that is never
    // gathered in the first place. Use it to make a small or low-priority enemy stop being
    // lockable from across the arena. Making a boss lockable from *further* out needs the gather
    // radius raised, which is a targeting-component change, not a per-target one.
    UPROPERTY(EditAnywhere, Category = "Targeting")
    float LockOnMaxDistanceClamp = 0.f;
};
```

**Constructor — the whole class's correctness lives here:**

```cpp
PrimaryComponentTick.bCanEverTick = false;
InitSphereRadius(60.f);
SetCollisionObjectType(COLLISION_TARGETABLE);
SetCollisionEnabled(ECollisionEnabled::QueryOnly);
SetCollisionResponseToAllChannels(ECR_Ignore);   // queried explicitly; blocks nothing
SetGenerateOverlapEvents(false);
SetCanEverAffectNavigation(false);
```

Two traps this encodes, both of which look like scoring bugs if missed:

1. **`Visibility` response must be `Ignore`** (covered by `SetCollisionResponseToAllChannels`).
   These spheres are large and chest-height. If they block `ECC_Visibility`, every LOS trace to a
   target standing behind another enemy is occluded by *that enemy's targeting sphere* — the
   symptom is "can't lock anything in a crowd."
2. **`SetGenerateOverlapEvents(false)`.** We only ever query this volume explicitly. Left on, every
   enemy fires begin/end overlap against every other enemy for nothing.

**`SetTargetable(bool)` — and the sync that has to go with it:**

```cpp
void SetTargetable(bool bNewTargetable)
{
    bTargetable = bNewTargetable;
    ApplyTargetableCollision();
}

void ApplyTargetableCollision()   // private
{
    SetCollisionEnabled(bTargetable ? ECollisionEnabled::QueryOnly
                                    : ECollisionEnabled::NoCollision);
}
```

`SetTargetable` is one call for a future death/stagger handler; an existing lock drops on its own
via Phase 4's validation.

**But the setter cannot be the only thing that applies it.** `bTargetable` is `EditAnywhere`, so a
designer can uncheck it on a placed dummy — and that path never calls `SetTargetable`. The bool
would say "not a target" while collision still says `QueryOnly`, and it would keep showing up in
every query with nothing on screen explaining why. Two more call sites close it:

- **`OnRegister()`** → `ApplyTargetableCollision()`, so the authored value is applied at startup.
- **`PostEditChangeProperty()`** (inside `#if WITH_EDITOR`) → same, so toggling the checkbox in the
  Details panel takes effect live instead of at the next PIE.

### Editor / config work

- Targetable **Object** channel declared in `DefaultEngine.ini`, default response `Ignore`.
- `BP_TargetDummy` at `/Game/Combat/BP_TargetDummy` — mesh + `UTargetableComponent`, sphere dragged
  to chest height. Needs a Live Coding compile first, since the component class must exist before it
  can be added.
- ~4 placed in the test level at varied distance and angle, one behind cover for the LOS path.

### Phase 1 is verifiable on its own

Place dummies, PIE, confirm the component resolves and `GetTargetPoint()` sits at the chest. No
gameplay change.

**Verify, don't assume:** that an object query returns these spheres with every response set to
`ECR_Ignore`. Object queries are expected to filter on object type and query-enabled state rather
than the response matrix, but that is the one load-bearing assumption in the constructor. If Phase
2's debug draw shows zero candidates, this is the first thing to check — before suspecting the
scorer.

---

## Phase 2 — Selection

### `FSlashTargetWeights` (USTRUCT)

One scoring function, two weight sets — the core idea from the plan, unchanged.

```cpp
USTRUCT(BlueprintType)
struct FSlashTargetWeights
{
    UPROPERTY(EditAnywhere, Category = "Targeting") float MaxDistance = 2000.f;
    UPROPERTY(EditAnywhere, Category = "Targeting") float MinDot = 0.34f;      // cone half-angle
    UPROPERTY(EditAnywhere, Category = "Targeting") float DistanceWeight = 0.45f;
    UPROPERTY(EditAnywhere, Category = "Targeting") float AngleWeight = 0.55f;
    UPROPERTY(EditAnywhere, Category = "Targeting") float StickinessBonus = 0.15f;
    UPROPERTY(EditAnywhere, Category = "Targeting") bool bRequireLineOfSight = true;
};
```

Defaults above are the persistent set. Soft-target set: `MaxDistance` 600, `MinDot` -0.17,
`DistanceWeight` 0.25, `AngleWeight` 0.75, `StickinessBonus` 0, `bRequireLineOfSight` false.

### `USlashTargetingComponent : UActorComponent`

`Source/Slash/SlashTargetingComponent.h/.cpp`

**The handle is the `UTargetableComponent`, not the actor.** It carries the aim point, so holding it
avoids a second lookup on every use. Actor accessors are provided for callers that think in actors.

**Does not tick itself** — `ASlashCharacter` calls `UpdateTargeting` at the top of its `Tick`, before
`UpdateFacing`. Same discipline as `USlashCameraRigComponent`, and Phase 4 depends on the ordering.

```cpp
public:
    void UpdateTargeting(float DeltaSeconds);

    // One-shot, no state, no hysteresis. IntentDirection is the stick direction when fresh,
    // camera forward otherwise.
    UTargetableComponent* FindSoftTarget(const FVector& IntentDirection) const;

    UFUNCTION(BlueprintPure, Category = "Targeting")
    UTargetableComponent* GetPersistentTarget() const;

    UFUNCTION(BlueprintPure, Category = "Targeting")
    UTargetableComponent* GetActiveTarget() const;   // Phase 2: == persistent. Phase 4: lock wins.

    UFUNCTION(BlueprintPure, Category = "Targeting")
    AActor* GetActiveTargetActor() const;

    UPROPERTY(BlueprintAssignable, Category = "Targeting")
    FOnTargetChanged OnTargetChanged;                // (UTargetableComponent* New, UTargetableComponent* Old)

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Targeting") FSlashTargetWeights SoftTargetWeights;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting") FSlashTargetWeights LockTargetWeights;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting") float RescanInterval = 0.1f;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting") int32 MaxTracedCandidates = 5;

private:
    void GatherCandidates(float Radius, TArray<UTargetableComponent*>& Out) const;
    bool ScoreCandidate(const UTargetableComponent* Candidate, const FVector& IntentOrigin,
                        const FVector& IntentDir, const FSlashTargetWeights& Weights,
                        const UTargetableComponent* Incumbent, float& OutScore) const;
    bool HasLineOfSight(const UTargetableComponent* Candidate, const FVector& From) const;
    UTargetableComponent* SelectBest(const FVector& IntentOrigin, const FVector& IntentDir,
                                     const FSlashTargetWeights& Weights,
                                     const UTargetableComponent* Incumbent) const;
    void RefreshPersistentTarget();

    TWeakObjectPtr<UTargetableComponent> PersistentTarget;
    float RescanAccumulator = 0.f;
```

**`ScoreCandidate` — filters cheapest-first, and returns false on rejection:**

```
reject if !Candidate->IsTargetable()
reject if Dist > (Clamp > 0 ? min(Clamp, Weights.MaxDistance) : Weights.MaxDistance)
reject if Dot(IntentDir, DirToTarget) < Weights.MinDot

Score = Weights.AngleWeight    * remap(Dot, MinDot..1, 0..1)
      + Weights.DistanceWeight * (1 - clamp(Dist / MaxDistance, 0, 1))
      + Candidate->TargetPriorityBias
      + (Candidate == Incumbent ? Weights.StickinessBonus : 0)
```

**`SelectBest`** scores all, sorts descending, then walks the sorted list running
`HasLineOfSight` until one passes — capped at `MaxTracedCandidates`. Traces are the expensive part;
never trace the whole sphere.

**`HasLineOfSight`** — single `LineTraceSingleByChannel` on `ECC_Visibility` from `From` to the
target point, ignoring self and the candidate's owner.

**`RefreshPersistentTarget`** runs on `RescanAccumulator >= RescanInterval` (~10 Hz), origin =
camera location, direction = camera forward, incumbent = current `PersistentTarget`. Broadcasts
`OnTargetChanged` only on an actual change.

**The stickiness bonus is not optional.** Without it two enemies at similar range and angle swap the
target every rescan.

### Debug

`slash.Targeting.Debug 1` (`FAutoConsoleVariable`) draws per rescan: a sphere at every candidate's
target point coloured by pass/reject, its score as text, the intent direction, and the active
target's LOS trace. `DrawDebugHelpers.h` is in `Engine` — no `Build.cs` change.

### Phase 2 is verifiable on its own

No gameplay change at all. Walk around with the debug cvar on and confirm the scores and the
selection behave — including that the choice doesn't flicker between two similar candidates.

---

## Phase 3 — Soft targeting into combat

No new classes. `ASlashCharacter` only. **This is the phase that changes how the game feels**, and
it needs no camera work — worth shipping even if lock-on is deferred.

```cpp
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    TObjectPtr<USlashTargetingComponent> TargetingComponent;

    // Clamp so an attack can't teleport facing across the screen.
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float MaxAttackFacingSnap = 60.f;

private:
    FVector GetAttackIntentDirection() const;   // CachedMoveDirection if fresh, else camera forward
    void SnapFacingToTarget(const UTargetableComponent* Target);
```

- Construct the component in the constructor alongside `ComboComponent` and `CameraRig`.
- Call `TargetingComponent->UpdateTargeting(DeltaSeconds)` at the **top** of `Tick`.
- `HandleComboNodeEntered`: after the `if (!Node) return;` guard and **above**
  `SetRootMotionMovementActive`, resolve a target (`GetActiveTarget()`, else
  `FindSoftTarget(GetAttackIntentDirection())`) and snap yaw toward it, clamped to
  `MaxAttackFacingSnap`. No target found → unchanged behaviour.

**The placement matters.** Root-motion nodes are excluded from the `LaunchCharacter` branch on
purpose, so for aerials the facing snap is the *only* steering the player gets. Putting it inside
the dash branch silently skips every aerial node.

`GetAttackIntentDirection` reuses the existing freshness test:
`(GFrameCounter - MoveInputFrame) <= 1 && !CachedMoveDirection.IsNearlyZero()`.

---

## Phase 4 — Lock-on

### `USlashTargetingComponent` additions

```cpp
UENUM()
enum class ELockBreakReason : uint8
{
    PlayerToggled,      // deliberate — no camera easing needed beyond the normal hand-back
    TargetInvalid,      // destroyed or !IsTargetable()
    OutOfRange,
    LostSight
};
```

```cpp
public:
    UFUNCTION(BlueprintCallable, Category = "Targeting") void ToggleLockOn();
    UFUNCTION(BlueprintCallable, Category = "Targeting") void SetLockOn(bool bEnabled);
    UFUNCTION(BlueprintPure,     Category = "Targeting") bool IsLockedOn() const;

    // Bounded, decaying look offset while locked. Large yaw flicks are intercepted for cycling
    // in Phase 5 — which is why this lives here rather than on the character.
    void AddLookInput(const FVector2D& LookAxis);

    bool GetLockCameraRotation(const FVector& BoomOrigin, FRotator& OutDesired) const;

    UPROPERTY(BlueprintAssignable, Category = "Targeting")
    FOnLockOnChanged OnLockOnChanged;                // (bool bLockedOn, UTargetableComponent* Target)

protected:
    // Deliberately larger than LockTargetWeights.MaxDistance — acquisition and retention need
    // hysteresis or the lock strobes at the boundary.
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float LockBreakDistance = 2400.f;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float LockLoseSightGrace = 0.6f;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float LockPitchBias = -8.f;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float LockLookOffsetYawMax = 25.f;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float LockLookOffsetPitchMax = 20.f;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float LockLookOffsetDecaySpeed = 4.f;

private:
    void ValidateLock(float DeltaSeconds);
    void ClearLockOn(ELockBreakReason Reason);       // one exit path, so camera hand-back is uniform

    TWeakObjectPtr<UTargetableComponent> LockTarget;
    float LookOffsetYaw = 0.f;
    float LookOffsetPitch = 0.f;
    float TimeWithoutSight = 0.f;
```

**Once locked, the target is validated, never re-scored.** Re-scoring makes the lock slide to
whatever is closest, which is not what the player asked for. Break conditions:

- target destroyed or `!IsTargetable()` → immediate
- distance > `LockBreakDistance` → immediate
- LOS blocked continuously > `LockLoseSightGrace` → break. A grace period is **not optional**;
  pillars and the target's own animation break LOS constantly.
- player toggles off

`ClearLockOn` also zeroes the look offsets so control rotation is handed back clean.

### `ASlashCharacter` changes

```cpp
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> LockOnAction;           // /Game/Input/IA_LockOn (already created)

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Lock")
    float LockCameraInterpSpeed = 8.f;

    UPROPERTY(EditDefaultsOnly, Category = "Movement")
    float LockFacingInterpSpeed = 12.f;

private:
    void UpdateLockCamera(float DeltaSeconds);
```

**1. `Look()` must suppress the engine path while locked.** This is the crux of the phase:

```cpp
if (TargetingComponent && TargetingComponent->IsLockedOn())
{
    TargetingComponent->AddLookInput(LookAxisVector);
    return;                              // no AddController*Input
}
```

`AddControllerYawInput` accumulates into `RotationInput`, which `APlayerController::UpdateRotation`
applies **after** the character's `Tick`. Leave it in and the `SetControlRotation` written during
`Tick` is overwritten by the player's own stick every frame — the lock reads as fighting the player.

**2. `UpdateFacing` restructure, not extension.** The lock branch goes **above** the
`bHasFreshInput` early return, not as a third arm of the existing `bIsInCombo ? stick : control`
ternary. Appending is the intuitive move and produces a lock that only faces the target while
you're moving.

**3. Tick order:** `UpdateTargeting` → `UpdateLockCamera` → `UpdateFacing` → `UpdateRig`.
`UpdateLockCamera` writes `Controller->SetControlRotation` from
`GetLockCameraRotation(BoomOrigin) + look offset`, interpolated at `LockCameraInterpSpeed`.

**4. Bind** `LockOnAction` on `ETriggerEvent::Started` → `ToggleLockOn()`, with the
`if (LockOnAction)` guard pattern used by `JumpAction` so it stays inert until assigned.

**Sprint needs no change.** `SprintForwardDotThreshold` compares against camera forward, which under
lock points at the target — so sprinting *at* the target works and strafing stays a jog. Confirm in
PIE before adding any "sprint breaks lock" rule.

**Strafe locomotion needs no AnimBP work.** `bOrientRotationToMovement` is already false, facing is
hand-rolled, and `Direction` / `BS_Jog2D` already exist.

### Still manual

`IMC_Gamepad` mapping for `IA_LockOn`. Hand-edit. Pick the key carefully: `Gamepad_RightThumbstick` already appears under `IA_Look`.

---

## Phase 5 — Camera and cycling

### `USlashCameraRigComponent`

Follows the existing push-in pattern exactly; nothing structural is new.

- `FSlashCameraRigInput`: add `bool bLockedOn`.
- Add `FSlashCameraProfile LockOnProfile` beside Default/Sprint/Combat/Air, and `float LockAlpha`
  beside `AirAlpha`/`CombatAlpha`.
- Suggested profile: arm ~300 (longer than Combat), FOV ~88 so two fighters fit, and
  `SocketOffset.Y` shifted toward centre — over-the-shoulder framing plus a locked target reads as
  lopsided.

Profiles blend as layers, not an exclusive state machine, so a locked-on sprinting player in the air
reads as a mix. No priority arbitration needed.

### `USlashTargetingComponent` — cycling

```cpp
    UFUNCTION(BlueprintCallable, Category = "Targeting")
    void CycleTarget(float DirectionSign);

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float CycleFlickThreshold = 1.5f;
    UPROPERTY(EditDefaultsOnly, Category = "Targeting|Lock") float CycleCooldown = 0.35f;
```

`AddLookInput` checks the flick threshold before feeding the offset; over it (and past the
cooldown), it calls `CycleTarget` instead. Candidates are projected onto the camera's right vector
and the nearest one on the flicked side of the current target wins. Hard lock without cycling is
unusable with more than two enemies.

---

## Build

No `Build.cs` change in any phase — `Engine`, `EnhancedInput` and `DrawDebugHelpers` are already
linked, and `PublicIncludePaths.Add(ModuleDirectory)` is already in place from the folder
reorganisation, so `#include "Targeting/TargetableComponent.h"` resolves. Worth protecting: a
`Build.cs` edit can't be applied by Live Coding and turns each iteration into an editor restart plus
a full rebuild.

Paths follow the feature-folder layout — see `SourceLayout.md`.

| File | Phase |
|---|---|
| `Targeting/TargetableComponent.h/.cpp` | 1 (new) |
| `Targeting/SlashTargetingComponent.h/.cpp` | 2 (new), extended in 4 and 5 |
| `Slash.h` | 1 (changed) — `COLLISION_TARGETABLE` alias |
| `Character/SlashCharacter.h/.cpp` | 3, 4 (changed) |
| `Camera/SlashCameraRigComponent.h/.cpp` | 5 (changed) |

Cross-folder includes are written from the module root, e.g. `#include "Targeting/TargetableComponent.h"`.

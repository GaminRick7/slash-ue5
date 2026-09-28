# Sword Hit Detection — Design Plan

Status: **in progress.** Phase 0 (montage windows) and Phase 1A (the `UComboComponent` open/close
pair) landed 2026-08-09; see [Phasing](#phasing) for what each covered. Nothing detects a hit yet —
`ASlashCharacter::HandleAttackHitWindow` still fires camera shake and hit stop *unconditionally*,
so swinging at empty air stutters the game exactly as hard as cleaving an enemy. There is no
health, no damage, and no hurtbox anywhere in `Source/Slash`.

This document supersedes `PolishPass.md` item 4 ("add an `AttackHit` notify"): that notify is
authored as a **window**, not a point — a single frame gives a sweep nothing to trace from.

## Goals

- **Swept, not instantaneous.** A sword connects across several frames of animation. Detection
  sweeps the blade volume from where it was last frame to where it is now, so a fast swing can't
  tunnel through a target between ticks.
- **One hit per target per swing.** A three-frame window must not deal damage three times.
- **Feedback follows contact.** Impact shake and hit stop fire only when the blade actually
  touches something. Whiffing costs nothing but the wind-up.
- **Data-driven damage.** Per-hit damage, knockback and feedback scaling live on the existing
  `FComboNode` in `UComboTreeAsset`, so rebalancing a combo stays a content change.
- **Decoupled consumers.** The trace component broadcasts `OnWeaponHit` with a full `FHitResult`.
  Camera feedback, damage, VFX and audio all bind to it independently; none of them know about
  each other.
- **Swappable hit geometry.** v1 sweeps against the enemy's `Pawn` capsule (decision below). What
  counts as hittable is one configurable object-type list, so upgrading to real hurtboxes later
  is a data change, not a rewrite.

## Non-goals (this pass)

- **Hit reactions.** No stagger montages, no ragdoll. `AEnemy` is a scaled `Cylinder` static mesh
  with no skeletal animation content — reactions need art authored first.
- **Player health.** `UHealthComponent` is written generically and can be dropped onto
  `ASlashCharacter` the day enemies can attack, but nothing attacks the player yet.
- **Blocking, parrying, i-frames, poise.** All want a hit *arbitration* layer between detection
  and damage. The `OnWeaponHit` → damage seam is where that layer will insert.
- **Locational damage.** Follows from the capsule decision — no bone names, no headshots.
- **Networking.** Everything here is server-authoritative-by-accident (single player). Nothing is
  replicated.

---

## Decision: what the blade collides with

**v1 sweeps against the `Pawn` object type — i.e. the enemy's `UCapsuleComponent`.**

This is the cheapest possible answer and it needs zero new setup: `AEnemy` never touches its
capsule's profile, so it inherits `ACharacter`'s default `Pawn` profile (object type `Pawn`,
`QueryOnly`+physics). Nothing else on the enemy is a `Pawn` — `CapsuleMesh` is `NoCollision`, and
`UTargetableComponent` is on the custom `Targetable` channel with `ObjectTypeName="WorldDynamic"`.
So an object-type query for `Pawn` returns **exactly one hit per enemy**, which makes dedup
trivial and unambiguous.

Be honest about what this costs:

| Consequence | Detail |
|---|---|
| Generous hits | The capsule is r=34, half-height=88 — a featureless cylinder wider than the character's actual body. Near-misses will register. |
| Impact points are wrong | `FHitResult::ImpactPoint` lands on the *capsule surface*, not the mesh. Spark VFX spawned there will float a few cm off the body. Mitigation: spawn effects at the blade's closest point instead of the reported impact point (`FMath::ClosestPointOnSegment` of the capsule centre against the blade segment). |
| No locational damage | One volume, one damage number. Headshots need the physics-asset upgrade. |

The upgrade path is deliberately one line. `UMeleeWeaponComponent` stores
`TArray<TEnumAsByte<EObjectTypeQuery>> HittableObjectTypes` as an `EditDefaultsOnly` property
defaulting to `{ Pawn }`. Adding a `UHurtboxComponent` on its own channel later means authoring
the component and changing that array in the Blueprint — the trace code is untouched.

---

## Architecture

Three new files in `Source/Slash/Combat/` (per `SourceLayout.md`; the engine-header shadowing
hazard that applies to `Camera/` does not apply here):

```
Combat/
  MeleeWeaponComponent.h / .cpp     the blade — mesh and detection in one. The new work.
  HealthComponent.h / .cpp          damage sink + death
  ComboTreeAsset.h                  (edit) add FAttackHitProfile to FComboNode
```

`UMeleeWeaponComponent` derives from `UStaticMeshComponent`. It *is* the sword — it renders the
mesh and sweeps itself. `ASlashCharacter::SwordComponent` is one, sitting at `RightHandSocket`;
no separate trace component exists. Rationale in the next section.

Data flow, end to end:

```
AM_SwordComboRegular
  └─ "AttackHit" Montage Notify Window (begin/end)
       │
       ▼
UComboComponent::HandleMontageNotifyBegin / End          (already subscribed to both delegates)
       │  broadcasts OnAttackWindowOpened(NodeId) / OnAttackWindowClosed(NodeId)
       ▼
ASlashCharacter                                          (decides *which* weapon is swinging)
       ▼
UMeleeWeaponComponent::BeginWindow(HitProfile) / EndWindow()
       │  ticks; sweeps itself; dedups
       ▼
OnWeaponHit(AActor* HitActor, const FHitResult& Hit, const FAttackHitProfile& Profile, int32 HitIndex)
       ├──────────────────────────────► UGameplayStatics::ApplyDamage → UHealthComponent (on AEnemy)
       │                                     └─ OnDeath → TargetableComponent->SetTargetable(false)
       └──────────────────────────────► ASlashCharacter::HandleWeaponHit
                                             ├─ CameraRig->PlayImpactShake()
                                             └─ ApplyHitStop()          ← only on HitIndex == 0
```

### Why the blade owns its own detection

The first draft of this document split it in two: a `UStaticMeshComponent` for the sword and a
separate `UWeaponTraceComponent` on the character, pointed at it via `SetWeapon(USceneComponent*)`.
Merging them is strictly better, and there is precedent for it in this codebase —
`UTargetableComponent : public USphereComponent` already takes an engine primitive, bolts domain
data and behaviour onto it, and gets attached to the mesh in a constructor. This is the same move.

What the merge buys:

- **No weapon pointer to go stale.** The component reads its own transform. No null checks, no
  `TWeakObjectPtr`, no "is my weapon still valid" branch every tick.
- **No reparent-mid-window bug.** A separate trace component aimed at a weapon whose attachment
  changes mid-swing — sheathing, an equip animation, a swap — would sweep from the old socket to
  the new one in a single tick: one invisible frame of a metre-long capsule through the character's
  body, hitting everything in the corridor. Removing the indirection removes the bug class.
- **Blade data lands where it belongs.** `TraceRadius` and the blade sockets describe a blade, not
  a character. On a shared trace component they'd have to move onto the weapon the day a second
  weapon exists anyway.
- **Dual wield is free.** Two components, two independent dedup sets, two windows. A single
  character-side tracer would have needed to manage N weapons by hand.

**What stays on the character:** the window driving. `ASlashCharacter` binds
`OnAttackWindowOpened`/`Closed` and forwards to the weapon. The weapon must not bind to
`UComboComponent` itself — it should know how to sweep and nothing else, and the character is the
only thing positioned to answer *which* weapon is swinging once there's more than one.

**The one real cost:** deriving from `UStaticMeshComponent` commits to static meshes. A weapon
needing a skeletal mesh (animated parts, morphs) would need a parallel class rather than a subclass
swap. `SM_Sword` is static and nothing suggests that changing. The alternative — a `USceneComponent`
with a child mesh — dodges it for an extra transform node and a deeper Blueprint hierarchy, which
isn't worth paying now.

Named `MeleeWeapon` rather than `Sword` because the sweep is identical for an axe or a spear; only
the two blade distances differ.

### Why the window rides `UComboComponent` rather than a custom notify class

Two viable shapes:

**A. Reuse the existing machinery (recommended for v1).** Author `AttackHit` as a **Montage Notify
Window** instead of a point notify. `UComboComponent` already subscribes to both
`OnPlayMontageNotifyBegin` and `OnPlayMontageNotifyEnd` and already dispatches on name
(`ComboComponent.cpp:151`) — the existing `AttackHitNotifyName` field keeps working, it just gains
an end handler. The component knows `CurrentNode`, so it hands the node's `FAttackHitProfile`
straight through with no lookup. Cost: ~15 lines, no new UCLASS, one more delegate.

**B. A custom `UAnimNotifyState_MeleeWindow`.** Self-describing track in the montage editor,
per-window parameters (trace radius, profile override, which weapon), and usable by actors that
have no combo tree.

Take A now. It reuses a wired path instead of adding a parallel one, and `UComboComponent` is
already designed to be the shared driver for enemies too. Graduate to B when *either* of these
becomes true — both are foreseeable, neither is here yet:

- A single montage section needs **two different** hit windows (a spin attack with distinct damage
  per revolution). Name-based dispatch degrades to `"AttackHit"` / `"AttackHit2"`, which is the
  signal to stop.
- Something wants a hit window **without** a combo tree driving it (an environmental trap, a
  weapon that swings on a timer).

`FAttackHitProfile` and `UMeleeWeaponComponent` are designed identically under either shape — only
who calls `BeginWindow` changes — so this is not a decision that has to be re-litigated later.

---

## Data model

### `FAttackHitProfile` — new struct in `ComboTreeAsset.h`

```cpp
USTRUCT(BlueprintType)
struct FAttackHitProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Hit")
    float Damage = 10.f;

    UPROPERTY(EditAnywhere, Category = "Hit")
    TSubclassOf<UDamageType> DamageType;

    /** Along the attacker's forward. Applied to the victim on hit. */
    UPROPERTY(EditAnywhere, Category = "Hit")
    float KnockbackSpeed = 0.f;

    /** Vertical component of the knockback. Positive lifts; launchers want this. */
    UPROPERTY(EditAnywhere, Category = "Hit")
    float KnockbackUpSpeed = 0.f;

    /** Scales the attacker's hit stop for a connecting swing. 0 disables it for this attack. */
    UPROPERTY(EditAnywhere, Category = "Hit")
    float HitStopScale = 1.f;

    /** Scales the impact shake on contact. Independent of FComboNode::CameraShakeScale. */
    UPROPERTY(EditAnywhere, Category = "Hit")
    float ImpactShakeScale = 1.f;

    /** 0 = unlimited. Caps how many distinct actors one window can damage. */
    UPROPERTY(EditAnywhere, Category = "Hit", meta = (ClampMin = "0"))
    int32 MaxTargetsPerWindow = 0;
};
```

Added to `FComboNode` as a single `UPROPERTY(EditAnywhere) FAttackHitProfile HitProfile;`.

**Leave `FComboNode::CameraShakeScale` exactly where it is.** It is not redundant with
`ImpactShakeScale`: it scales the FOV punch fired on *node entry* (`SlashCharacter.cpp:412`),
which is the camera leaning into the wind-up and is correctly unconditional. `ImpactShakeScale`
scales the shake on *contact*, which is not. The split is the whole point of this system — a
finisher can have a big anticipatory lean and a modest impact, or the reverse.

### `UMeleeWeaponComponent`

Traces come out of this component's own +Z axis — it's the sword, so there's nothing to point it at.

```cpp
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SLASH_API UMeleeWeaponComponent : public UStaticMeshComponent
{
public:
    void BeginWindow(const FAttackHitProfile& Profile);
    void EndWindow();
    bool IsWindowOpen() const { return bWindowOpen; }

    UPROPERTY(BlueprintAssignable, Category = "Combat")
    FOnWeaponHit OnWeaponHit;   // (AActor* HitActor, const FHitResult& Hit, int32 HitIndex)

protected:
    /** What the blade can hit. Defaults to { Pawn } — see the hurtbox decision. */
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace")
    TArray<TEnumAsByte<EObjectTypeQuery>> HittableObjectTypes;

    /**
     * The cutting edge, authored as two sockets on the mesh asset. Required — detection is inert
     * without both. See "Where the blade comes from" below.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace")
    FName BladeBaseSocket = "BladeBase";

    UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace")
    FName BladeTipSocket = "BladeTip";

    /** Blade thickness. Also buys rotation budget — see the coverage rule. */
    UPROPERTY(EditDefaultsOnly, Category = "Combat|Trace")
    float TraceRadius = 10.f;
};
```

### `UHealthComponent`

```cpp
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class SLASH_API UHealthComponent : public UActorComponent
{
public:
    UFUNCTION(BlueprintPure, Category = "Health")
    bool IsAlive() const { return Health > 0.f; }

    UFUNCTION(BlueprintPure, Category = "Health")
    float GetHealthPercent() const;

    UPROPERTY(BlueprintAssignable, Category = "Health")
    FOnHealthChanged OnHealthChanged;   // (float NewHealth, float Delta, AActor* Instigator)

    UPROPERTY(BlueprintAssignable, Category = "Health")
    FOnDeath OnDeath;                   // (AActor* Killer)

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Health")
    float MaxHealth = 100.f;

    /** Seconds the actor lingers after death, for a death anim / dissolve. 0 = destroy now. */
    UPROPERTY(EditDefaultsOnly, Category = "Health")
    float CorpseLifeSpan = 3.f;

    float Health = 0.f;
};
```

Binds `GetOwner()->OnTakeAnyDamage` in `BeginPlay` and does the arithmetic there. Using the
engine's damage pipeline (`UGameplayStatics::ApplyDamage`) rather than a bespoke `IDamageable`
interface buys `AActor::TakeDamage` overrides, damage-type classes, and instigator/causer
plumbing for free, and costs nothing.

---

## The trace

### Where the blade comes from

Two sockets on `SM_Sword`, `BladeBase` at the guard and `BladeTip` at the point, authored with the
gizmo in the Static Mesh Editor. Not authored distances along an axis. This was the deciding
argument:

- **The 0.027 scale disappears.** The sword component carries `SetRelativeScale3D(FVector(0.027f))`
  (`SlashCharacter.cpp:72`) to shrink a ~4000uu source mesh to a ~110cm blade. `GetSocketLocation`
  returns **world** space, composed through the component transform, so that factor is applied for
  you and never appears in anything a human authors. Any scheme built on local offsets has to work
  around it.
- **Placement is visual.** Drag two points onto the blade instead of guessing that the edge starts
  at 15 and ends at 108 and checking in PIE.
- **It travels with the asset.** Every future weapon carries its own correct values, so adding an
  axe is an art task, not a tuning task. This is what makes the per-weapon story work without
  moving anything into a data asset.
- **It drops the "+Z is the blade" assumption.** Nothing has to agree about which local axis the
  cutting edge runs along — the two sockets *are* the axis. A mesh that imported sideways just gets
  its sockets placed accordingly.

The rejected alternative was a real `UCapsuleComponent` child, sized with the gizmo and read each
frame. It gives the same WYSIWYG authoring but inherits the 0.027 scale (a 46.5 half-height would
be authored as `1722`), adds a component whose collision settings must be remembered and disabled,
and still doesn't do the sweeping — you'd read four numbers off it and call `MakeCapsule` anyway.
Sockets buy the same thing for none of that.

**A missing socket fails silently and must not.** `GetSocketLocation` with an unknown name returns
the *component's* transform rather than erroring, so both endpoints collapse to the same point at
the hilt: a zero-length blade that hits nothing, with no warning. Validate both with
`DoesSocketExist` in `BeginPlay`, log an error, and refuse to open windows — a hard no-op is far
easier to diagnose than a sword that silently stopped connecting.

### Per tick, while a window is open

1. Resolve the blade's world-space endpoints from the sockets:

   ```cpp
   const FVector Start = GetSocketLocation(BladeBaseSocket);
   const FVector End   = GetSocketLocation(BladeTipSocket);
   ```

   See "Where the blade comes from" immediately below for why these are sockets rather than
   authored distances.

2. Sweep one blade-shaped capsule from where the blade was to where it is:

   ```cpp
   const FVector Mid        = (Start + End) * 0.5f;
   const float   HalfHeight = FVector::Dist(Start, End) * 0.5f;

   // MakeCapsule builds along its own local Z, so orient that axis down the blade.
   const FQuat   BladeRot   = FRotationMatrix::MakeFromZ(End - Start).ToQuat();

   // A sweep translates but never rotates its shape, so the blade's turn over this frame has to
   // collapse into one orientation. Slerping to the midpoint centres the error across the arc
   // instead of biasing it to one end.
   const FQuat Rot = FQuat::Slerp(PrevRotation, BladeRot, 0.5f);

   World->SweepMultiByObjectType(Hits, PrevMid, Mid, Rot, ObjectParams,
                                 FCollisionShape::MakeCapsule(TraceRadius, HalfHeight),
                                 QueryParams);
   ```

   Everything derives from the two socket positions — centre, length, and axis. `MakeFromZ` leaves
   X and Y arbitrary, which is harmless: a capsule is rotationally symmetric about its Z.
   Note the sweep's two positional arguments are capsule **centres**, not blade endpoints.

   Guard `HalfHeight >= TraceRadius`. Below that a capsule is geometrically degenerate and the
   engine collapses it toward a sphere — fine for a 93uu blade, a real trap for a short dagger.

3. **Rotation budget, and why this shape choice has one.**

   Because the capsule translates without rotating, consecutive frames leave a wedge between them.
   A point at distance `d` from the centre sits up to `d·sin(θ/2)` from the nearest capsule axis,
   worst case at the tip. So coverage is gap-free while:

   ```
   HalfHeight · sin(θ/2) <= TraceRadius
   ```

   With a ~93uu blade (`HalfHeight` 46.5) and `TraceRadius = 10`: `θ <= ~25°` of blade rotation per
   frame. At
   60fps that's a 1500°/s budget — comfortably above a typical fast slash (~1200°/s). At 30fps the
   same swing needs 40°/frame and **opens a gap**. `FComboNode::PlayRate` multiplies angular
   velocity directly, so a node at 1.5× spends the budget 50% faster.

   This is the deliberate trade. One capsule sweep is cheap and simple, but its correctness is
   framerate- and playrate-dependent in a way the alternative isn't (see below). Two escapes if it
   ever bites, in order of preference:

   - **Raise `TraceRadius`.** The budget scales linearly with it. Costs hit generosity.
   - **Substep.** Slerp the rotation and lerp the centre into N sub-sweeps within the frame. Easy
     here precisely *because* you hold both transforms — the interpolation is already the thing
     the capsule couldn't express, so doing it explicitly recovers the accuracy.

   > **The alternative, recorded in case it's ever needed.** Decompose the blade into N points and
   > sphere-sweep each along its own displacement, tiling the swept region with capsules. Its
   > coverage rule is `TraceRadius >= BladeLength / (N-1) / 2` — no θ in it, so accuracy is
   > independent of rotation rate, and it degrades gracefully on spins (rotation about the blade's
   > middle, where the centre barely moves and a single capsule sweep collapses toward a static
   > test). Cost is comparable: sphere-vs-capsule is a closed-form quadratic where capsule-vs-capsule
   > needs GJK. Chosen against for v1 on simplicity at a 60fps target. Swapping is contained to this
   > one loop — the window plumbing, dedup set, prev-state and debug draw are identical either way.

4. On the first tick of a window, seed `PrevMid`/`PrevRotation` from the current transform. The
   sweep degenerates to a near-zero-length one; don't rely on it catching anything, because the
   window opens before the blade reaches a target and real detection starts on tick two.

5. Filter each hit: skip if already in `HitActorsThisWindow`, skip if there's no
   `UHealthComponent`, skip if that component reports dead. Then add to the set, broadcast
   `OnWeaponHit` with an incrementing `HitIndex`, and stop early once `MaxTargetsPerWindow` is
   reached.

`QueryParams.AddIgnoredActor(GetOwner())` is mandatory, not hygiene — the player's own capsule is
`Pawn`, and the blade starts inside it during the wind-up. Without the ignore, every swing hits
the attacker on frame one.

### Ticking after the pose, not before

`PrimaryComponentTick.bCanEverTick = true`, but **the component must tick after the skeletal mesh
evaluates its pose**, or every trace reads a blade transform one frame stale and hits land
visibly behind the sword. In `BeginPlay`:

```cpp
if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
{
    if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
    {
        PrimaryComponentTick.AddPrerequisite(Mesh, Mesh->PrimaryComponentTick);
    }
}
```

An explicit prerequisite beats moving to `TG_PostPhysics` — it states the actual dependency
(the pose), survives the mesh changing tick groups, and doesn't drag the trace past physics for
no reason.

Also set `SetComponentTickEnabled(false)` at construction and flip it on only inside a window.
There is no reason to pay for a tick between swings.

### Debug visualisation

Gate on a cvar rather than a `#if WITH_EDITOR` — being able to flip it in a packaged build is
worth the two lines:

```cpp
static TAutoConsoleVariable<int32> CVarMeleeTraceDebug(
    TEXT("slash.MeleeTrace.Debug"), 0,
    TEXT("0 off, 1 draw the swept capsule for one frame, 2 persistent"), ECVF_Cheat);
```

`DrawDebugCapsule` at both ends of the sweep — the capsule at `PrevMid`/`PrevRotation` and at
`Mid`/current — plus a line joining the two centres. Those two capsules and the volume between them
*are* the tested region, so if a hit looks wrong the picture shows why immediately. Mark each
reported hit with a sphere at `ImpactPoint`.

Drawing both ends also makes the rotation budget visible: when the two capsules stop overlapping
near their tips, you are past `HalfHeight · sin(θ/2) <= TraceRadius` and a target can slip through
the wedge between them.

---

## Rewiring the feedback

`ASlashCharacter::HandleAttackHitWindow` (`SlashCharacter.cpp:433`) is deleted. Its body moves to
a new `HandleWeaponHit` bound to `UMeleeWeaponComponent::OnWeaponHit`:

```cpp
void ASlashCharacter::HandleWeaponHit(AActor* HitActor, const FHitResult& Hit, int32 HitIndex)
{
    const FAttackHitProfile& Profile = /* current node's profile */;

    if (CameraRig)
    {
        CameraRig->PlayImpactShake(Profile.ImpactShakeScale);
    }

    // Only the first target of a window stops time. A cleave through three enemies broadcasts
    // three times; stacking three hit stops would freeze the swing for a third of a second.
    if (HitIndex == 0)
    {
        ApplyHitStop(HitStopDuration * Profile.HitStopScale, HitStopTimeDilation);
    }

    UGameplayStatics::ApplyDamage(HitActor, Profile.Damage, GetController(), this,
                                  Profile.DamageType);
}
```

Two behaviour changes the moment this lands, both intended and both immediately noticeable:

- **Whiffs stop stuttering.** Today every swing into empty air fires a 90ms hit stop at 0.15
  dilation. After this, only connections do. Expect the combo to feel considerably faster and
  looser on its own, which may want `HitStopDuration` tuned *up* to compensate on real hits.
- **The wind-up FOV punch is unchanged.** It stays on `HandleComboNodeEntered`, unconditional.
  That's correct — it's anticipation, and anticipation happens before you know whether you'll
  connect.

### Death, and the targeting system

`UHealthComponent::OnDeath` on an `AEnemy` must do four things, in this order:

1. `TargetableComponent->SetTargetable(false)` — **do not skip this.** `USlashTargetingComponent`
   holds a persistent target and `ASlashCharacter::LeapTowardTarget` leaps at
   `GetPersistentTarget()->GetTargetPoint()`. Without this, the player keeps locking onto and
   leaping at corpses. `TargetingSystem_Plan.md:572` already anticipated exactly this hook.
2. `GetCapsuleComponent()->SetCollisionEnabled(NoCollision)` — the capsule *is* the hurtbox, so
   this is what stops the blade from hitting a dead enemy. The `IsAlive()` filter in the trace
   covers the same frame; this covers everything after.
3. Broadcast `OnDeath` for VFX/audio/score to bind to.
4. `SetLifeSpan(CorpseLifeSpan)`.

---

## Phasing

Each phase is independently testable. Don't merge them.

**Phase 0 — content: the `AttackHit` window. ✅ done 2026-08-09.** In `AM_SwordComboRegular` and
`AM_SwordComboAerial`, a **Montage Notify Window** (not a point notify) named `AttackHit` on each
attack section, spanning the frames the blade is actually swinging through its arc. This supersedes
`PolishPass.md` item 4, which specified a point notify. Nothing detects hits until this exists —
`AttackHitNotifyName` is inert without it.

**Phase 1 — detection only.** No damage, no health. Three steps:

- **1A ✅ done 2026-08-09.** `UComboComponent` emits both ends of the window. `OnAttackHitWindow`
  split into `OnAttackWindowOpened`/`OnAttackWindowClosed`, and `HandleMontageNotifyEnd` gained
  the `AttackHitNotifyName` branch — placed *before* the chain-window guard, which rejects every
  other name and would otherwise swallow it. The two handlers were renamed from
  `HandleComboWindowOpened`/`Closed`, since both dispatch two different notifies by name and the
  old names described only one. `ASlashCharacter` still binds the open event alone to the existing
  `HandleAttackHitWindow`, so feedback stays unconditional until Phase 3 — Phase 1 changes nothing
  about how the game feels.
- **1B-content.** Two sockets on `SM_Sword` in the Static Mesh Editor: `BladeBase` at the guard,
  `BladeTip` at the point. Blocks 1B the way Phase 0 blocked 1A — the component reads its blade
  geometry from these and does nothing useful without them.
- **1B.** `UMeleeWeaponComponent` — the sweep, the dedup set, the window state, the debug cvar.
- **1C.** `ASlashCharacter::SwordComponent` replaces the old `WeaponMesh`; bind `OnAttackWindowOpened`/`Closed` to
  `BeginWindow`/`EndWindow`, plus the defensive `EndWindow()` from `HandleComboNodeEntered` and
  `HandleComboEnded` (hazard 1).

Verify by eye with `slash.MeleeTrace.Debug 1`: the drawn capsule should track the blade with no lag,
and log lines should appear when it crosses `BP_Enemy` and only then.

**Do not change an existing component's class — rename it.** `WeaponMesh` was a
`CreateDefaultSubobject` that `BP_SlashCharacter` inherited, and a Blueprint stores its per-component
overrides keyed by *name*. Repointing that name at `UMeleeWeaponComponent` left the CDO holding
`UStaticMeshComponent`-typed data, so on every load the engine nulled the reference
(`LogProperty: Warning: Serialized Class ... Reference will be nulled`) and the component showed up
in the Components panel with an empty Details panel. Restarting does not help: the stale data lives
in the `.uasset`, and an inherited C++ component cannot be deleted from a Blueprint. The escape was
to rename the member to `SwordComponent` — including the `TEXT("SwordComponent")` subobject name,
which is what serialization actually keys on — so it had no prior data to conflict with. Cost: the
`SM_Sword` assignment and the viewport-tuned relative rotation had to be re-authored once.

**Phase 2 — damage and death. Code landed 2026-08-09, not yet play-verified.**
`UHealthComponent` binds the owner's `OnTakeAnyDamage` rather than exposing an apply function, so
anything routed through the engine's damage path feeds it and the sword never learns it exists. It
broadcasts only — reacting to death is the owner's job, since a corpse means different things for an
enemy and for the player. `AEnemy::HandleDeath` does the three steps in order: `SetTargetable(false)`
first, capsule collision off, then `SetLifeSpan(CorpseLifeSpan)`.

`ASlashCharacter::HandleWeaponHit` calls `UGameplayStatics::ApplyDamage` with a placeholder
`AttackDamage` (25.f, so four hits kill a 100hp dummy) until `FAttackHitProfile` moves damage onto
the node. The trace now filters to actors with a live `UHealthComponent`.

Verify a dummy dies in the expected number of swings and that lock-on releases when it does.

**Phase 3 — feedback re-gating. Partly done 2026-08-09**, pulled forward ahead of Phase 2.
`HandleWeaponHit` is bound to `OnWeaponHit` and owns the impact shake and hit stop; hit stop is
gated on `HitIndex == 0` so a cleave doesn't stack one per enemy. `HandleAttackWindowOpened` now
only opens the window. Whiffs cost nothing, so `HitStopDuration` likely wants raising — it was
tuned when every swing paid it.

Still outstanding: `FAttackHitProfile` on the six nodes in `DA_Combo_Sword`. Until it exists,
`HandleWeaponHit` reuses `FComboNode::CameraShakeScale`, which also drives the wind-up FOV punch —
anticipation and impact sharing one number is exactly what the profile's `ImpactShakeScale` exists
to separate.

**Later.** Knockback (needs the victim to be a `ACharacter` with `LaunchCharacter`), hurtbox
upgrade, hit reactions, player health, hit arbitration (block/parry/i-frames).

---

## Known hazards

These are the things that will actually bite. Ordered by how likely they are to cost an evening.

1. **A cancelled window never closes.** This is the big one, and the existing combo design invites
   it. `technique-ue-combo-montage-gotchas` #4 documents the cancel-early pattern: the player's
   input calls `Montage_JumpToSection` away from the current section immediately. If that happens
   while a hit window is open, the `NotifyEnd` for the abandoned window may never fire — so
   `bWindowOpen` stays true, `HitActorsThisWindow` never clears, and the blade keeps tracing
   through the next attack with a stale dedup set. **`UMeleeWeaponComponent::EndWindow()` must be
   called defensively from `OnComboNodeEntered` and `OnComboEnded`, not only from the notify's
   end handler.** `EndWindow` must be idempotent. The same guard style already used in
   `HandleMontageNotifyEnd` (`if (!bWindowOpen) return;`) applies.

2. **Every socket in this system fails silently, in two different places.** Neither reports an
   error; both fall back to a component transform, so a typo produces wrong geometry rather than a
   crash. Validate all three names in `BeginPlay`.

   - **`RightHandSocket`, on the parent skeletal mesh.** The sword component attaches to it in the
     constructor, and `SlashCharacter.h:85` already notes that attaching to a missing socket falls
     back to the mesh origin. The visual bug (sword at the character's feet) is obvious; the
     detection bug (a 110cm blade sweeping out of the character's centre, hitting everything
     nearby) is less so. Check the **skeletal mesh**, not the Skeleton asset — `RightHandSocket`
     is defined on `UAL1_Standard`, and neither Skeleton asset has any sockets at all.
     `DoesSocketExist` covers both, which is why it's the right call to make.
   - **`BladeBase`/`BladeTip`, on `SM_Sword`.** `GetSocketLocation` with an unknown name returns
     the component's own transform, so both endpoints collapse to the same point and the capsule
     becomes zero-length. The sword connects with nothing, silently. Refuse to open windows at all
     if either is missing — a hard no-op with an error log is far easier to diagnose.

3. **Hit stop dilates the attacker, and the window ticks through it.** `ApplyHitStop` sets
   `CustomTimeDilation` on the character, so the montage — and therefore the blade — nearly
   freezes while the window stays open, for ~6× the real time. Harmless, because the dedup set
   already blocks re-hits and the near-stationary blade sweeps almost nothing new. Worth knowing
   before it looks like a bug in a profile capture. Note also that the *victim* is not dilated;
   dilating both for the same duration is a phase-2 polish that makes impacts read much heavier.

4. **Root-motion aerial nodes.** `Aerial_1/2/3` run in `MOVE_Flying` with the montage driving the
   capsule. No interaction with tracing — the blade transform is read from the mesh either way —
   but the character is moving fast during those, which is exactly the case the sweep exists for.
   Test aerials specifically; a per-frame displacement of several hundred uu is where a naive
   implementation tunnels. Watch the *rotation* rate too — an uppercut that whips the blade through
   a wide arc is where the capsule's 25°/frame budget gets spent.

5. **`MaxTargetsPerWindow` ordering is arbitrary.** `SweepMultiByObjectType` returns hits in sweep
   order — i.e. by distance along the capsule's *travel*, which has nothing to do with which enemy
   the player was aiming at. A cap of 1 in a crowd picks whoever the blade reached first in time,
   not the most plausible target. Fine for now; if it ever matters, sort candidates by distance to
   the blade segment before applying the cap.

6. **One notify name across three montages.** `AM_SwordComboHeavy` exists in content but isn't
   referenced by `DA_Combo_Sword`. If it gets wired up later, its `AttackHit` windows will be
   picked up automatically — which is right, but means authoring the notify is now a required
   step for any new attack montage, not an optional polish one.

---

## Open questions

- **Does the sword stop at walls?** v1 queries object types only, so it ignores world geometry
  entirely — the blade passes through a pillar and hits the enemy behind it. Adding a
  `LineTraceSingleByChannel` on `Visibility` from the attacker to each hit, and rejecting blocked
  ones, is the fix if it ever reads badly. Not worth pre-building.
- **Team filtering.** Every actor with a `UHealthComponent` is currently a valid target, so the
  moment a second faction exists this needs a check. `IGenericTeamAgentInterface` is the engine's
  answer and costs little to adopt early.

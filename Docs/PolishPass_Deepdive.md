# Polish pass — deep dive

Companion to `PolishPass.md`. That one is the summary and the editor checklist; this one explains
*why* each piece is built the way it is, with the code and the engine mechanics behind it.

Written 2026-07-28. Nothing here has been compiled yet — see [Compile-safety notes](#9-compile-safety-notes-lwc)
for what was verified statically and what wasn't.

---

## Contents

1. [The shape of the problem](#1-the-shape-of-the-problem)
2. [The camera rig](#2-the-camera-rig)
3. [Camera shakes](#3-camera-shakes)
4. [Movement and facing](#4-movement-and-facing)
5. [Jump](#5-jump)
6. [Landing](#6-landing)
7. [Combat feel](#7-combat-feel)
8. [Combo component changes](#8-combo-component-changes)
9. [Compile-safety notes (LWC)](#9-compile-safety-notes-lwc)
10. [What is deliberately inert](#10-what-is-deliberately-inert)

---

## 1. The shape of the problem

The starting point was a working locomotion + combo setup with the camera logic living directly in
`ASlashCharacter::Tick`. It looked like this:

```cpp
const float TargetArmLength = BaseArmLength + SprintArmLengthOffset * SprintAlpha;
CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetArmLength, DeltaSeconds, CameraInterpSpeed);

const float TargetFOV = BaseFOV + SprintFOVOffset * SprintAlpha;
FollowCamera->FieldOfView = FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaSeconds, CameraInterpSpeed);

if (SprintAlpha > 0.f)
{
    SprintBobTime += DeltaSeconds * SprintBobFrequency;
    const float VerticalBob = FMath::Sin(SprintBobTime * 2.f * PI) * SprintBobAmplitude * SprintAlpha;
    const float LateralBob  = FMath::Sin(SprintBobTime * PI + 1.3f) * SprintBobAmplitude * 0.5f * SprintAlpha;
    CameraBoom->SocketOffset = FVector(0.f, LateralBob, VerticalBob);
}
else
{
    SprintBobTime = 0.f;
    CameraBoom->SocketOffset = FVector::ZeroVector;
}
```

Three separate problems, only one of which is about taste:

**It doesn't scale past two states.** Each camera property is interpolated independently against
its own target. At two states (idle / sprint) that's fine. At four (idle / sprint / combat / air)
you get N properties × M states of interacting `FInterpTo` calls, and because they're independent
they can be at different points in their transitions at the same moment. The camera drifts out of
step with itself.

**`SocketOffset` was already spoken for.** This is the one that forced the issue. `SocketOffset` is
the field an over-the-shoulder camera uses — it's the offset in boom space that pushes the camera
off the character's centre line. The bob code writes it every frame and *zeroes it* in the `else`
branch. So the moment I added a shoulder offset, the camera would have snapped from
over-the-shoulder to dead-centre the instant `SprintAlpha` hit zero. The bob and the shoulder
offset cannot both own that field.

**Everything was on the character.** `ASlashCharacter` was doing input, movement, combat state,
*and* camera work. The camera work is the part with the most knobs and the least to do with the
character specifically.

So: extract the camera into its own component, and re-express the states as blendable profiles
rather than independent interpolations.

---

## 2. The camera rig

### 2.1 Profiles

`FSlashCameraProfile` bundles everything the rig owns into one struct, so a "camera state" is a
single value that can be interpolated as a unit:

```cpp
USTRUCT(BlueprintType)
struct FSlashCameraProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Camera Profile")
    float ArmLength = 320.f;

    /** Offset in boom space. Y is the over-the-shoulder push, Z the eye-line lift. */
    UPROPERTY(EditAnywhere, Category = "Camera Profile")
    FVector SocketOffset = FVector(0.f, 55.f, 55.f);

    /** Offset in world space, applied to the boom origin before the arm swings out. */
    UPROPERTY(EditAnywhere, Category = "Camera Profile")
    FVector TargetOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Camera Profile")
    float FieldOfView = 85.f;

    UPROPERTY(EditAnywhere, Category = "Camera Profile")
    float CameraLagSpeed = 15.f;

    UPROPERTY(EditAnywhere, Category = "Camera Profile")
    float RotationLagSpeed = 25.f;

    /** How fast this profile's weight eases in and out. Higher is snappier. */
    UPROPERTY(EditAnywhere, Category = "Camera Profile")
    float BlendSpeed = 6.f;
};
```

`SocketOffset` vs `TargetOffset` is worth being precise about, because mixing them up is a common
way to get a camera that feels subtly wrong:

- **`TargetOffset`** is applied in **world space** to the boom's origin *before* the arm extends.
  It moves the point the camera orbits around. Raising its Z raises the pivot, so the camera looks
  down at the character more as it orbits.
- **`SocketOffset`** is applied in **boom space** at the far end, *after* the arm extends. It slides
  the camera sideways/up relative to where the arm put it, without moving the orbit centre. This is
  what produces over-the-shoulder framing: the character stays centred in the orbit, but the camera
  sits off to one side of them.

The rig uses `SocketOffset` for the shoulder and leaves `TargetOffset` at zero, exposed for tuning.

Four profiles are configured in the constructor. The values are all defaults you can override per
Blueprint; the reasoning is what matters:

```cpp
// Neutral over-the-shoulder: close enough to read the character's weight, offset far enough
// that the body doesn't sit on the crosshair line.
DefaultProfile.ArmLength = 300.f;
DefaultProfile.SocketOffset = FVector(0.f, 55.f, 55.f);
DefaultProfile.FieldOfView = 85.f;

// Sprint pulls back, widens, and drifts toward centre — the shoulder framing that reads as
// composed at a walk reads as cramped at speed. Lower lag speed lets the camera trail.
SprintProfile.ArmLength = 375.f;
SprintProfile.SocketOffset = FVector(0.f, 32.f, 62.f);
SprintProfile.FieldOfView = 96.f;
SprintProfile.CameraLagSpeed = 9.f;

// Combat tightens in over the shoulder and narrows the FOV, which reads as focus and makes
// the swing arcs fill more of the frame.
CombatProfile.ArmLength = 255.f;
CombatProfile.SocketOffset = FVector(0.f, 62.f, 48.f);
CombatProfile.FieldOfView = 78.f;
CombatProfile.CameraLagSpeed = 20.f;

// Airborne drops the camera so the character sits high in frame and the ground stays visible.
AirProfile.ArmLength = 345.f;
AirProfile.SocketOffset = FVector(0.f, 48.f, 30.f);
AirProfile.FieldOfView = 88.f;
```

Three of these encode real cinematography rules:

- **Sprint widens FOV and pulls back.** Wider FOV exaggerates peripheral motion, which is most of
  the perceived speed. Pulling back is what stops the widened FOV from feeling like the camera is
  falling into the character's back.
- **Sprint moves toward centre** (`Y: 55 → 32`). A hard shoulder offset is fine when you're
  composing a shot at walking pace; at speed it puts the character's body across the direction of
  travel, which is exactly where you want to see.
- **Combat narrows FOV and closes in.** Narrow FOV compresses depth and makes the character bigger
  in frame, and the tighter boom means a sword arc sweeps across more of the screen.

### 2.2 Layered blending, not a state machine

This is the core design decision. The obvious implementation is a priority test — pick the highest
priority active state, blend to it. That produces visible cuts: a sprinting player who gets knocked
airborne would snap from the sprint profile to the air profile, because only one can win.

Instead the profiles are applied as **layers**, each with its own weight:

```cpp
AirAlpha    = FMath::FInterpTo(AirAlpha,    Input.bInAir    ? 1.f : 0.f, DeltaSeconds, AirProfile.BlendSpeed);
CombatAlpha = FMath::FInterpTo(CombatAlpha, Input.bInCombat ? 1.f : 0.f, DeltaSeconds, CombatProfile.BlendSpeed);

// Layered rather than exclusive. A sprinting player knocked into the air reads as a blend of
// both states instead of a cut to whichever one won a priority test. Combat is applied last
// because pulling in for a swing should override the sprint framing, not average with it.
FSlashCameraProfile Target = DefaultProfile;
Target = BlendProfiles(Target, SprintProfile, FMath::Clamp(Input.SprintAlpha, 0.f, 1.f));
Target = BlendProfiles(Target, AirProfile, AirAlpha);
Target = BlendProfiles(Target, CombatProfile, CombatAlpha);
```

Read it as a Photoshop layer stack. `DefaultProfile` is the background. Each subsequent line
composites another profile over the accumulated result at its own opacity. `BlendProfiles` is a
plain per-field `Lerp`:

```cpp
FSlashCameraProfile USlashCameraRigComponent::BlendProfiles(const FSlashCameraProfile& From, const FSlashCameraProfile& To, float Alpha)
{
    if (Alpha <= 0.f)
    {
        return From;
    }

    FSlashCameraProfile Result;
    Result.ArmLength = FMath::Lerp(From.ArmLength, To.ArmLength, Alpha);
    Result.SocketOffset = FMath::Lerp(From.SocketOffset, To.SocketOffset, Alpha);
    // ... every field
    return Result;
}
```

Two consequences worth understanding:

**Order matters, and it's the compositing order.** Combat is applied last, so at `CombatAlpha == 1`
the result *is* the combat profile regardless of what sprint and air contributed underneath. That's
deliberate: pulling in for a swing should override the sprint framing rather than average with it.
Sprint is applied first because it's the most "ambient" of the three.

**Partial weights interpolate meaningfully.** At `SprintAlpha 0.6` and `AirAlpha 0.3` you get a
genuine mix, not a fight between two systems.

The `if (Alpha <= 0.f) return From;` early-out isn't just a micro-optimisation — it means an
inactive layer is a true no-op rather than a `Lerp` with alpha 0, which matters if a field is ever
made non-linear later.

### 2.3 Why there's no second smoothing pass

The comment in the code flags this explicitly:

```cpp
// No second smoothing pass on the result: every weight above is already eased, so damping
// the blended profile as well would just add latency to a camera that is already gliding.
```

The obvious next step after computing `Target` is `Current = FInterpTo(Current, Target, ...)` and
then apply `Current`. That would be wrong here. `SprintAlpha` is already eased by the character
(`SprintSpeedInterpSpeed`), and `AirAlpha`/`CombatAlpha` are eased by `FInterpTo` two lines up. So
`Target` is *already* a smoothly-varying value. Damping it again is a second low-pass filter in
series: the camera would still get where it's going, but with visible extra latency and a mushier
onset. One easing stage, applied to the weights, is the right amount.

### 2.4 Camera lag, and why it's bounded

```cpp
CameraBoom->bEnableCameraLag = true;
CameraBoom->bUseCameraLagSubstepping = true;
CameraBoom->CameraLagMaxTimeStep = 1.f / 60.f;
CameraBoom->CameraLagMaxDistance = CameraLagMaxDistance;   // 140
```

Camera lag makes the boom's origin chase the character's position instead of being rigidly welded
to it, which gives the camera weight.

`CameraLagMaxDistance` is the important one, and it's set because of a specific interaction with the
combat system. `ASlashCharacter::HandleComboNodeEntered` does:

```cpp
LaunchCharacter(GetActorForwardVector() * Node->DashSpeed, true, false);
```

with `DashSpeed` up to 800 on the third light attack. Unbounded camera lag looks great at walking
pace and then **slingshots** on a dash: the character teleports forward relative to the lagged
camera, the camera plays catch-up over the following frames, and you get a rubber-band. Clamping
the lag to 140 units means the camera can trail, but never by more than a bounded amount, so a dash
reads as the camera being *pulled* rather than snapped.

`bUseCameraLagSubstepping` with a 1/60 max timestep makes the lag framerate-independent. Without it,
the effective lag distance changes with framerate — the camera feels different at 30fps and 120fps,
which is exactly the kind of thing that makes a game feel cheap on some machines and not others.

### 2.5 Why rotation lag is off

```cpp
/**
 * Off by default. The boom takes its rotation straight from the control rotation, so
 * rotation lag delays the player's own look input — that reads as latency, not weight.
 */
UPROPERTY(EditDefaultsOnly, Category = "Camera|Lag")
bool bEnableRotationLag = false;
```

This is the one "polish" feature I deliberately did *not* turn on, and it's worth being clear why,
because it looks like an obvious win.

The spring arm has `bUsePawnControlRotation = true`. That means the boom's rotation is taken
directly from the player's control rotation each frame — it *is* the player's look input. Enabling
rotation lag inserts a damping filter between the stick and the camera's orientation. The result is
not "weighty"; it's **input latency**. You push the stick and the camera starts turning a few frames
later.

Rotation lag is genuinely useful on a boom whose rotation is driven by something other than direct
player input — an AI-controlled camera, a lock-on target, or `bOrientRotationToMovement`-style
follow. It's exposed as a toggle so it's available if the camera ever gets driven that way, but it
stays off while the player owns the rotation directly.

`CameraRotationLagSpeed` is still written every frame from the profile, so if you flip the toggle
the per-state values are already there.

### 2.6 Collision

```cpp
CameraBoom->bDoCollisionTest = true;
CameraBoom->ProbeSize = CameraProbeSize;   // 12
```

Collision testing matters more with an over-the-shoulder camera than with a centred one. A centred
boom sweeps a fairly predictable arc behind the character; a shoulder-offset boom puts the camera
closer to walls on one side, so it clips into geometry far more readily. The 12-unit probe is
smaller than the engine default (12 vs. the typical 12–15 range) to reduce the "camera shoves
itself forward for no visible reason" artefact when brushing past thin geometry.

### 2.7 The additive springs

Two effects sit *on top of* the resolved profile rather than being part of it, because they're
impulses rather than states.

**FOV punch** — a simple exponential decay back to zero:

```cpp
FOVPunch = FMath::FInterpTo(FOVPunch, 0.f, DeltaSeconds, FOVPunchRecoverySpeed);
```

**Pitch dip** — a proper damped harmonic oscillator:

```cpp
// Damped spring rather than a decay curve, so a landing overshoots slightly on the way back
// up. That tiny counter-swing is most of what sells the weight.
const float DipAcceleration = (-PitchDipStiffness * PitchDip) - (PitchDipDamping * PitchDipVelocity);
PitchDipVelocity += DipAcceleration * DeltaSeconds;
PitchDip += PitchDipVelocity * DeltaSeconds;
```

That's Hooke's law with viscous damping, integrated with semi-implicit Euler. `-k·x` pulls the
offset back toward zero proportionally to how far it is; `-c·v` bleeds off velocity. With
`PitchDipStiffness = 240` and `PitchDipDamping = 22` the system is *under-damped*, which is the
entire point: it overshoots slightly on the way back up before settling. A plain decay curve
(`FInterpTo` to zero) would settle monotonically, and monotonic settling reads as a camera being
dragged back rather than a body absorbing an impact and rebounding.

The impulse is fed into **velocity**, not position:

```cpp
void USlashCameraRigComponent::AddPitchDip(float Degrees)
{
    // Fed into velocity, not position: an impulse rings the spring, whereas writing the offset
    // directly teleports the camera and then eases back from the jump.
    PitchDipVelocity += Degrees;
}
```

Writing `PitchDip += Degrees` would instantaneously displace the camera and then ease back — a
visible one-frame jump. Adding to velocity means the camera *accelerates* into the dip over several
frames and back out, which is what a physical impact does.

Both are applied at the end, over the profile's values:

```cpp
FollowCamera->SetFieldOfView(Profile.FieldOfView + FOVPunch);
FollowCamera->SetRelativeRotation(FRotator(-PitchDip, 0.f, 0.f));
```

The pitch dip goes on **`FollowCamera`'s relative rotation**, not the boom's. It has to: the boom's
rotation is overwritten from the control rotation every frame by `bUsePawnControlRotation`, so
anything written there is discarded. `FollowCamera` has `bUsePawnControlRotation = false`, so its
relative rotation composes on top of whatever the boom's socket hands it. The negation
(`-PitchDip`) is because positive pitch in Unreal is nose-*up*, and a landing should dip the view
down.

### 2.8 Push-based update, not component tick

```cpp
USlashCameraRigComponent::USlashCameraRigComponent()
{
    // Driven from the owner's Tick instead, so the rig always reads state that is already
    // final for the frame rather than racing the character's own tick.
    PrimaryComponentTick.bCanEverTick = false;
    ...
}
```

The component does not tick itself. `ASlashCharacter::Tick` calls it explicitly, last, after all
movement state for the frame is settled:

```cpp
UpdateSprintState(DeltaSeconds);
UpdateFacing(DeltaSeconds);
UpdateAirState(DeltaSeconds);
// ... lean calculation ...

if (CameraRig)
{
    FSlashCameraRigInput RigInput;
    RigInput.SprintAlpha = SprintAlpha;
    RigInput.bInCombat = bIsInCombo;
    RigInput.bInAir = GetCharacterMovement()->IsFalling();
    CameraRig->UpdateRig(DeltaSeconds, RigInput);
}
```

If the component ticked independently, its ordering relative to the character's tick would depend
on tick group registration details, and it could read a half-updated `SprintAlpha` — producing a
one-frame lag that's invisible in normal play and maddening when it isn't. Explicit invocation makes
the ordering a property of the code rather than of the tick registration.

The rig also never reaches *back* into the character. State comes in through
`FSlashCameraRigInput`; nothing in the rig casts to `ASlashCharacter`. That's what makes it
droppable onto any pawn with a spring arm and a camera.

---

## 3. Camera shakes

### 3.1 Why not the engine's shake patterns

Unreal ships `UWaveOscillatorCameraShakePattern` and `UPerlinNoiseCameraShakePattern`, which do
roughly what I needed. I didn't use them, and the reason is a build-system one rather than a
technical one.

Those classes live in `Engine/Plugins/Cameras/EngineCameras` — **a plugin, not the Engine module**.
The plugin is `"EnabledByDefault": true`, so no `.uproject` edit would be needed, but linking
against it still requires:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { ..., "EngineCameras" });
```

A `Slash.Build.cs` change **cannot be applied by Live Coding**. It would have turned one
`Ctrl+Alt+F11` into an editor restart plus a full rebuild. Since the whole rest of this pass was
Live-Coding-patchable, spending that cost on an oscillator was a bad trade.

`UCameraShakeBase` and `UCameraShakePattern` themselves *are* in the Engine module
(`Camera/CameraShakeBase.h`), so subclassing them directly costs nothing.

### 3.2 How UE5 camera shakes actually work

Worth spelling out, because the architecture is not obvious from the class names.

`UCameraShakeBase` **contains no shake logic at all**. It's a container that holds a
`UCameraShakePattern` and delegates everything to it:

```cpp
// UCameraShakeBase::UpdateAndApplyCameraShake, paraphrased
FCameraShakePatternUpdateParams Params(InOutPOV);
Params.DeltaTime = DeltaTime;
Params.ShakeScale = ShakeScale;
Params.DynamicScale = Alpha;

FCameraShakePatternUpdateResult Result;
RootShakePattern->UpdateShakePattern(Params, Result);
```

So there is nothing overridable on the shake class — all behaviour lives in the pattern. Subclasses
install their pattern by swapping the class of a subobject the base already creates. The base
constructor does:

```cpp
RootShakePattern = static_cast<UCameraShakePattern*>(ObjectInitializer.CreateDefaultSubobject(
        this, TEXT("RootShakePattern"), UCameraShakePattern::StaticClass(),
        nullptr,  // No class to create by default
        false,    // Not required
        ...));
```

which means a subclass overrides it by name, exactly the way `ULegacyCameraShake` does:

```cpp
USlashCameraShake::USlashCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<USlashOscillatorShakePattern>(TEXT("RootShakePattern")))
{
}
```

Getting that string wrong fails silently — you get a shake with a null pattern that does nothing.

**Two more things the API does not do for you:**

The pattern's virtuals are declared **`private`** in `UCameraShakePattern`. That's the NVI pattern:
the public `UpdateShakePattern` is non-virtual and forwards to the private virtual
`UpdateShakePatternImpl`. Overriding a private virtual from a derived class is legal C++ (you just
can't *call* it), and it's what the engine's own patterns do.

`UpdateAndApplyCameraShake` passes the camera modifier's alpha through as `DynamicScale`, but it
does **not** apply the pattern's own blend-in/blend-out. `FCameraShakeInfo::BlendIn`/`BlendOut` are
informational. The pattern owns its blending. Engine patterns use `FCameraShakeState` for this;
mine tracks it directly:

```cpp
float USlashOscillatorShakePattern::ComputeBlendWeight() const
{
    const float BlendIn = (BlendInTime > 0.f) ? FMath::Clamp(ElapsedTime / BlendInTime, 0.f, 1.f) : 1.f;

    float BlendOut = 1.f;
    if (bStopping)
    {
        BlendOut = (BlendOutTime > 0.f) ? FMath::Clamp(StopBlendOutRemaining / BlendOutTime, 0.f, 1.f) : 0.f;
    }
    else if (Duration > 0.f && BlendOutTime > 0.f)
    {
        BlendOut = FMath::Clamp((Duration - ElapsedTime) / BlendOutTime, 0.f, 1.f);
    }

    return BlendIn * BlendOut;
}
```

Two blend-out paths, because a shake can end two ways: run out its fixed `Duration`, or be stopped
early by `StopCameraShake` (which is how the sprint shake ends — it has no duration at all).

### 3.3 The oscillator

Each axis is a channel with an amplitude and a frequency:

```cpp
USTRUCT(BlueprintType)
struct FSlashShakeChannel
{
    GENERATED_BODY()

    FSlashShakeChannel() = default;
    FSlashShakeChannel(float InAmplitude, float InFrequency)
        : Amplitude(InAmplitude), Frequency(InFrequency) {}

    UPROPERTY(EditAnywhere, Category = "Shake")
    float Amplitude = 0.f;

    UPROPERTY(EditAnywhere, Category = "Shake")
    float Frequency = 1.f;
};
```

Sampling:

```cpp
float USlashOscillatorShakePattern::SampleChannel(const FSlashShakeChannel& Channel, float& InOutPhase, float DeltaTime) const
{
    if (Channel.Amplitude == 0.f)
    {
        return 0.f;
    }

    InOutPhase += DeltaTime * Channel.Frequency;

    const float Sample = bUsePerlinNoise
        ? FMath::PerlinNoise1D(InOutPhase)
        : FMath::Sin(InOutPhase * 2.f * PI);

    return Channel.Amplitude * Sample;
}
```

Note the phase is **accumulated by delta time**, not computed from an absolute clock. That means
changing frequency at runtime doesn't cause a discontinuity — with `sin(t · f)` a frequency change
jumps the phase; with an accumulator it doesn't.

The `2π` is only in the sine branch, so `Frequency` is in Hz there. For Perlin it's noise-units per
second, which isn't the same unit — a deliberate trade to keep the sine path intuitive.

**Sine vs. Perlin** is a per-shake choice and it matters:

- **Perlin** for sustained shakes. Over a long hold, a sine is a metronome — the eye picks up the
  periodicity within a second or two and it starts reading as a mechanical wobble. Perlin drifts.
- **Sine** for short jolts. Over a 0.28-second impact shake, Perlin is as likely to hand back a
  near-zero stretch as a peak, so the hit sometimes just... doesn't land. A sine guarantees a clean
  excursion.

**Randomised start phases**, set in `StartShakePatternImpl`:

```cpp
// Randomised start phases. Without them every shake of a given class begins at the same
// point in its waveform, and repeated hits read as one identical canned bump.
PhaseLocationX = FMath::FRand() * 100.f;
PhaseLocationY = FMath::FRand() * 100.f;
// ... one per channel
```

Per-channel, not one shared seed — otherwise the axes stay phase-locked and the shake moves along a
fixed diagonal line instead of describing a shape.

### 3.4 Live amplitude on the sprint shake

The sprint shake runs **indefinitely** (`Duration = 0`) and its strength is modulated every frame:

```cpp
void USlashCameraRigComponent::UpdateSprintShake(float SprintAlpha)
{
    APlayerCameraManager* CameraManager = GetCameraManager();
    if (!CameraManager || !SprintShakeClass)
    {
        return;
    }

    if (SprintAlpha > KINDA_SMALL_NUMBER)
    {
        if (!SprintShakeInstance.IsValid())
        {
            SprintShakeInstance = CameraManager->StartCameraShake(SprintShakeClass, 1.f);
        }

        // Amplitude tracks the sprint ramp, so the drift grows and fades with the sprint itself
        // instead of snapping to full strength the instant the threshold is crossed.
        if (UCameraShakeBase* Shake = SprintShakeInstance.Get())
        {
            Shake->ShakeScale = SprintAlpha;
        }
    }
    else if (SprintShakeInstance.IsValid())
    {
        CameraManager->StopCameraShake(SprintShakeInstance.Get(), false);
        SprintShakeInstance.Reset();
    }
}
```

This works because `UCameraShakeBase::ShakeScale` is a public transient property that's read fresh
on **every** update and folded into `Params.GetTotalScale()`. Holding the instance and writing to it
each frame is a supported way to drive a shake from a continuous game value.

The instance is held as a `TWeakObjectPtr`, not a raw pointer — the camera manager owns the shake's
lifetime and can recycle it, so a strong reference would be wrong and a raw one unsafe.

`StopCameraShake(..., bImmediately = false)` lets the pattern's blend-out run instead of cutting
the drift dead.

`bSingleInstance = true` on the sprint shake class means a redundant start restarts the existing
instance rather than layering a second copy of the same drift.

### 3.5 The three shakes

```cpp
// Sprint — deliberately small. At a 400-unit boom this is a drift you feel rather than see; push
// the amplitudes and it stops reading as speed and starts reading as a bumpy road.
Oscillator->LocationY = FSlashShakeChannel(0.9f, 3.1f);
Oscillator->LocationZ = FSlashShakeChannel(1.4f, 4.7f);
Oscillator->Pitch     = FSlashShakeChannel(0.25f, 4.1f);
Oscillator->Yaw       = FSlashShakeChannel(0.20f, 2.9f);
Oscillator->Roll      = FSlashShakeChannel(0.45f, 2.3f);
```

The frequencies are deliberately **mutually non-harmonic** (3.1, 4.7, 4.1, 2.9, 2.3). If they were
integer multiples of each other, the combined motion would repeat on a short cycle and become
visibly periodic. Irrational-ish ratios make the composite pattern take a very long time to repeat.

Roll has the largest rotational amplitude (0.45 vs. 0.25/0.20). Roll is the least disorienting
rotation axis on a third-person camera — pitch and yaw fight the player's aim, roll doesn't.

```cpp
// Impact — sharp and short.
Oscillator->Duration = 0.28f;
Oscillator->BlendInTime = 0.f;      // no ramp; a hit is instantaneous
Oscillator->bUsePerlinNoise = false;
Oscillator->FieldOfView = FSlashShakeChannel(1.20f, 9.f);
```

`BlendInTime = 0` is the point: any blend-in on an impact shake softens the very frame that's
supposed to be the hardest. The FOV channel adds a subtle breathing to the punch that pure
positional shake doesn't give.

```cpp
// Land — vertical-dominant: a landing is a floor impact, so the lateral channels stay token.
Oscillator->LocationZ = FSlashShakeChannel(3.4f, 12.f);
Oscillator->LocationY = FSlashShakeChannel(0.8f, 9.f);
```

Z amplitude is 4× Y, because the impulse direction is vertical. Shakes that ignore the direction of
the impulse are the main reason canned shakes feel generic.

---

## 4. Movement and facing

### 4.1 The `Move()` restructure

The old version bailed out at the top during a combo:

```cpp
void ASlashCharacter::Move(const FInputActionValue& Value)
{
    if (bIsInCombo)
    {
        return;   // <-- nothing recorded, nothing known
    }
    ...
}
```

Total input lockout during attacks is the least modern thing in an action game. But the fix isn't
simply to delete the check — the aerial combo drives the capsule from **root motion** in
`MOVE_Flying`, and adding movement input on top would fight it directly.

The resolution is to separate *intent* from *translation*. The early-return moved down, and the
direction is recorded before it:

```cpp
const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

// Recorded before the combo check, and separately from GetLastMovementInputVector(), because
// an attack suppresses AddMovementInput but the character still needs to know where the
// player is pointing in order to steer the swing.
CachedMoveDirection = (ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X).GetSafeNormal();
MoveInputFrame = GFrameCounter;

// Translation stays locked during an attack — adding input here would fight the aerial
// combo, which drives the capsule from root motion. Only the facing is steerable.
if (bIsInCombo)
{
    return;
}
```

**Why a separate cached vector instead of `GetLastMovementInputVector()`?** Because that function
returns the input vector accumulated by `AddMovementInput` — and during a combo we deliberately
don't call it. So the engine's own record of "where is the player pointing" goes to zero exactly
when we need it.

**Why the frame counter?** Enhanced Input's `Triggered` event fires every frame while the stick is
held, but stops firing when it's released — there's no "released" callback that would let us clear
the cache. Comparing against `GFrameCounter` gives a freshness test:

```cpp
const bool bHasFreshInput = (GFrameCounter - MoveInputFrame) <= 1 && !CachedMoveDirection.IsNearlyZero();
```

A tolerance of 1 frame rather than 0 because input is processed in the player controller's tick and
the pawn's tick may run before or after it depending on registration order. Zero would produce
intermittent single-frame dropouts.

### 4.2 Facing

```cpp
// Out of combat the body squares up to the camera, which is what makes the 2D jog blendspace
// read as strafing. Mid-combo it instead turns toward the stick, so a swing can be redirected
// without the camera having to be swung around first.
const FRotator TargetRotation = bIsInCombo
    ? FRotator(0.f, CachedMoveDirection.Rotation().Yaw, 0.f)
    : FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f);

// Slower the faster you go: a sprint that can pivot as sharply as a standstill looks weightless.
float InterpSpeed = FMath::Lerp(TurnInterpSpeed, SprintTurnInterpSpeed, SprintAlpha);
if (bIsInCombo)
{
    InterpSpeed = CombatTurnInterpSpeed;
}

SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaSeconds, InterpSpeed));
```

Two different targets, and the switch is the important part:

- **Out of combat**, the body faces the **camera**. This is what makes strafing work — the character
  stays squared to the view while moving sideways, which is what the `BS_Jog2D` blendspace's
  `Direction` input expects. Changing this would break the existing locomotion.
- **In combat**, the body faces the **stick**. That's directional attack steering: you can redirect
  a swing mid-combo by pushing the stick, without having to swing the camera around first. It's the
  thing that makes combo systems feel responsive rather than on-rails.

The old code used a hardcoded `10.f`. Now it lerps 14 → 6 with the sprint ramp, so a standstill
pivot is crisp and a full sprint arcs. A sprint that can turn as sharply as a standstill is one of
the most common "this feels weightless" tells. Combat gets its own slower 3.5 — enough authority to
matter, little enough that you can't spin freely mid-swing.

### 4.3 Lean

```cpp
// Lean is derived from how fast the body actually turned this frame rather than from raw
// stick deflection, so it stays in step with the rotation the mesh is playing. Scaled by
// ground speed so pivoting on the spot doesn't lean into nothing.
const float CurrentYaw = static_cast<float>(GetActorRotation().Yaw);
const float YawDelta = FMath::FindDeltaAngleDegrees(PreviousYaw, CurrentYaw);
PreviousYaw = CurrentYaw;

float TargetLean = 0.f;
if (DeltaSeconds > 0.f && LeanYawRateScale > 0.f)
{
    const float GroundSpeed = static_cast<float>(GetVelocity().Size2D());
    const float SpeedRatio = FMath::Clamp(GroundSpeed / FMath::Max(WalkSpeed, 1.f), 0.f, 1.f);
    TargetLean = FMath::Clamp((YawDelta / DeltaSeconds) / LeanYawRateScale, -1.f, 1.f) * SpeedRatio;
}
LeanAmount = FMath::FInterpTo(LeanAmount, TargetLean, DeltaSeconds, LeanInterpSpeed);
```

Three decisions:

**Measured, not commanded.** Lean comes from the yaw the body *actually* turned, not from stick
deflection. The body's rotation is itself damped by `RInterpTo`, so a stick-derived lean would lead
the mesh — the character would lean before it turned. Measuring the result keeps them locked
together.

**`FindDeltaAngleDegrees`, not subtraction.** Handles the 179° → -179° wraparound. A plain
`CurrentYaw - PreviousYaw` produces a 358° spike once per revolution, which would slam the lean to
its clamp.

**Scaled by ground speed.** Leaning is a response to lateral acceleration. Pivoting on the spot
generates yaw rate but no lateral acceleration, so without the speed scale the character would lean
hard while standing still — which looks like a bug.

This is exposed as a `BlueprintPure` getter for the AnimBP to consume; nothing in C++ uses it.

### 4.4 CMC retuning

```cpp
Movement->MaxAcceleration = 2400.f;

// Separate braking friction so the character can accelerate crisply and still stop short,
// instead of one friction value being a compromise between the two.
Movement->bUseSeparateBrakingFriction = true;
Movement->BrakingFriction = 6.f;
Movement->GroundFriction = 8.f;

// Boosted air control only near-stationary, so a standing jump can still be steered while a
// sprinting leap keeps its committed arc.
Movement->AirControlBoostMultiplier = 2.f;
Movement->AirControlBoostVelocityThreshold = 25.f;
```

By default the CMC uses `GroundFriction` for both acceleration and braking, so one number has to
serve two opposed goals — high friction stops you sharply but also makes you feel sticky when
accelerating. `bUseSeparateBrakingFriction` decouples them.

The air control boost pair is a subtle one: `AirControlBoostMultiplier` only applies below
`AirControlBoostVelocityThreshold` of horizontal speed. So a standing jump (starting near zero
lateral velocity) gets doubled air control and is steerable, while a running leap keeps its
committed ballistic arc. That's the behaviour good platformers have and it's almost free.

The constructor was also refactored to hoist `GetCharacterMovement()` into a local — it was being
called seven times.

---

## 5. Jump

None of this runs yet: no `IA_Jump` asset exists. It's written and wired so that creating the asset
is the only remaining step.

### 5.1 Coyote time

"Coyote time" is the grace window after walking off a ledge during which a jump press still counts.
Without it, players are certain they pressed jump in time — and they did, by a frame or two.

Unreal has a **purpose-built hook** for this, which is not obvious:

```cpp
bool ASlashCharacter::CanJumpWhileFalling() const
{
    // Coyote time. The engine calls this at exactly the point where it would otherwise consume
    // the ground jump for having already left the floor. Restricted to a genuine ledge-walk-off:
    // first jump only, still descending, and not mid aerial-combo.
    return !bRootMotionMovementActive
        && JumpCurrentCount == 0
        && TimeSinceLeftGround <= CoyoteTime
        && GetCharacterMovement()->Velocity.Z <= 0.f;
}
```

`ACharacter::CanJumpWhileFalling()` is virtual, returns `false` by default, and is consulted in
exactly two places — `CheckJumpInput` and `JumpIsAllowedInternal`:

```cpp
// ACharacter::CheckJumpInput
const bool bFirstJump = JumpCurrentCount == 0;
if (bFirstJump && CharacterMovement->IsFalling() && !CanJumpWhileFalling())
{
    JumpCurrentCount++;   // consume the ground jump for having left the floor
}
```

That is precisely the moment where the engine decides "you're airborne, so your ground jump is
gone." Returning `true` there suppresses the consumption. Reimplementing this via
`CanJumpInternal_Implementation` would have been fighting the engine instead of using it.

The four conditions narrow it to a genuine ledge walk-off: first jump only, still descending (so it
can't be abused mid-jump), and not while a root-motion combo owns the capsule.

### 5.2 Jump buffering — and the ordering trap

Buffering is the mirror image: a jump pressed slightly *before* landing should fire on touchdown
rather than being dropped.

**My first implementation of this was wrong**, and the bug is a good illustration of why UE's jump
system needs care. The obvious place to spend the buffer is `Landed()`:

```cpp
// WRONG — do not do this
void ASlashCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    if (JumpBufferRemaining > 0.f) { Jump(); }   // silently does nothing
}
```

Here's why it fails. `Landed` is called from `ProcessLanded`, which runs inside `PhysFalling`,
which runs inside `PerformMovement`. And `PerformMovement` calls `ClearJumpInput` at its end:

```cpp
void ACharacter::ClearJumpInput(float DeltaTime)
{
    if (bPressedJump)
    {
        JumpKeyHoldTime += DeltaTime;
        if (JumpKeyHoldTime >= GetJumpMaxHoldTime())   // default JumpMaxHoldTime is 0.0f
        {
            bPressedJump = false;
        }
    }
    ...
}
```

`Jump()` only sets `bPressedJump = true` and zeroes `JumpKeyHoldTime`; the actual jump happens in
`CheckJumpInput`. With the default `JumpMaxHoldTime` of `0.0f`, the condition
`JumpKeyHoldTime >= 0.f` is immediately true — so the flag set in `Landed` is cleared later in the
*same* `PerformMovement`, before `CheckJumpInput` ever sees it. The jump vanishes with no error.

The fix is to inject at the point where a real press would have landed. `CheckJumpInput` is virtual
and is called by `ControlledCharacterMove` immediately *before* `PerformMovement`:

```cpp
void ASlashCharacter::CheckJumpInput(float DeltaTime)
{
    // The buffered press has to be spent *here*, not in Landed(). Landed() runs inside
    // PerformMovement, and PerformMovement calls ClearJumpInput at its end — with the default
    // JumpMaxHoldTime of 0 that immediately clears bPressedJump again, so a jump queued from
    // Landed would be wiped before this function ever ran. CheckJumpInput is called just before
    // PerformMovement, which is exactly where a real press would have landed.
    if (JumpBufferRemaining > 0.f && !bPressedJump && !bIsInCombo && CanJump())
    {
        JumpBufferRemaining = 0.f;
        Jump();
    }

    Super::CheckJumpInput(DeltaTime);
}
```

`!bPressedJump` avoids stomping a real press in the same frame. `CanJump()` gates it so the buffer
only fires when a jump is actually legal — which, thanks to the coyote override, can also be true
mid-fall.

The timer is decremented in `UpdateAirState`, and set in `StartJump` only when the press couldn't
be honoured immediately:

```cpp
if (CanJump())
{
    Jump();
    return;
}

// Pressed too early — hold it and spend it on touchdown.
JumpBufferRemaining = JumpBufferTime;
```

### 5.3 Variable jump height

```cpp
void ASlashCharacter::StopJump(const FInputActionValue& Value)
{
    StopJumping();

    // Variable jump height: releasing while still rising trims the arc rather than committing
    // to the full JumpZVelocity every time.
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (Movement->IsFalling() && Movement->Velocity.Z > 0.f)
    {
        Movement->Velocity.Z *= JumpCutMultiplier;   // 0.45
    }
}
```

The `Velocity.Z > 0.f` check is what makes this a *cut* rather than a general slowdown — it only
applies while still ascending. Releasing after the apex does nothing.

The alternative is `JumpMaxHoldTime`, where holding the key keeps applying force. The cut approach
gives a more immediate response: the arc changes on the frame you release, rather than depending on
how long you held.

### 5.4 Asymmetric gravity

```cpp
// Heavier on the way down only. A symmetric arc reads as floaty at any jump height; the
// asymmetry is what makes the descent feel committed. Skipped while a root-motion combo owns
// the capsule, where the movement mode is flying and gravity is not in play anyway.
const bool bFastFalling = Movement->IsFalling() && Movement->Velocity.Z < 0.f && !bRootMotionMovementActive;
Movement->GravityScale = bFastFalling ? FallGravityScale : BaseGravityScale;
```

A physically correct parabola feels wrong in a game — the hang time on the way down reads as
floatiness no matter how you tune the jump height. Nearly every platformer since Mario applies
extra gravity on descent. `FallGravityScale = 1.9` roughly halves the descent time relative to the
rise.

`BaseGravityScale` is captured in `BeginPlay` from the CMC rather than hardcoded to 1.0, so it
respects whatever the Blueprint sets.

---

## 6. Landing

```cpp
void ASlashCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);

    const float ImpactSpeed = PeakFallSpeed;
    PeakFallSpeed = 0.f;
    TimeSinceLeftGround = 0.f;
    GetCharacterMovement()->GravityScale = BaseGravityScale;

    if (CameraRig && ImpactSpeed > LandShakeMinSpeed)
    {
        const float Strength = FMath::Clamp(
            (ImpactSpeed - LandShakeMinSpeed) / FMath::Max(LandShakeMaxSpeed - LandShakeMinSpeed, 1.f), 0.f, 1.f);

        CameraRig->PlayLandShake(Strength);
        CameraRig->AddPitchDip(LandMaxPitchDip * Strength);
    }
}
```

**Why `PeakFallSpeed` instead of reading velocity here?** By the time `Landed` fires, the CMC has
already resolved the collision and the vertical velocity has been zeroed or reflected. The actual
impact speed has to be sampled during the fall, which `UpdateAirState` does:

```cpp
if (Movement->IsFalling())
{
    PeakFallSpeed = FMath::Max(PeakFallSpeed, static_cast<float>(-Movement->Velocity.Z));
}
```

`Max` rather than last-value because a fall can be interrupted (a ledge graze, a brief re-ground)
and we want the worst of it.

**Why a threshold and a remap rather than linear scaling?** `LandShakeMinSpeed = 400` means small
hops produce nothing at all — shaking the camera on every tiny step-down is worse than not shaking
it. Above that, the response is remapped to 0–1 across the 400–1400 range and clamped, so a fall
from any great height maxes out rather than growing without bound.

The two effects are complementary: the shake is high-frequency texture, the pitch dip is the
low-frequency body of the impact. Either alone reads as incomplete.

---

## 7. Combat feel

### 7.1 FOV punch on the wind-up

```cpp
// A narrowing FOV kick on the wind-up. Applied on entry rather than on contact because it
// reads as the camera leaning into the swing, not as the hit itself.
if (CameraRig)
{
    CameraRig->AddFOVPunch(AttackFOVPunch * Node->CameraShakeScale);
}
```

`AttackFOVPunch` defaults to **-3.5** — negative, so the FOV *narrows*. Narrowing pulls the subject
closer and reads as focus/anticipation. Widening would read as recoil, which belongs on the impact,
not the wind-up.

This fires on node entry (the start of a swing), which is why it's the anticipation beat. The impact
beat is separate.

### 7.2 Per-node scaling

```cpp
// Scales this hit's camera feedback — FOV kick, impact shake and hit stop. Raise it on
// finishers so the last hit of a chain lands harder than the jab that opened it.
UPROPERTY(EditAnywhere, Category = "Combo")
float CameraShakeScale = 1.f;
```

Every combat effect is multiplied by this. A flat feedback level across a 3-hit chain makes the
chain feel monotonous; escalating it is most of what makes a finisher feel like a finisher.

Because it has a C++ default of `1.f`, existing `DA_Combo_Sword` entries deserialise with a sane
value and need no asset edit to keep working.

### 7.3 Hit stop

```cpp
void ASlashCharacter::ApplyHitStop(float Duration, float TimeDilation)
{
    if (Duration <= 0.f)
    {
        return;
    }

    // CustomTimeDilation rather than global dilation: it freezes the swing without dragging the
    // camera and the rest of the world into it, and the timer that clears it runs on undilated
    // world time, so the stop is always exactly as long as it was asked to be.
    CustomTimeDilation = FMath::Clamp(TimeDilation, 0.01f, 1.f);
    GetWorldTimerManager().SetTimer(HitStopTimerHandle, this, &ASlashCharacter::EndHitStop, Duration, false);
}
```

Hit stop is the brief freeze on contact that makes a hit feel like it connected with mass. Two
implementation choices:

**`CustomTimeDilation`, not `SetGlobalTimeDilation`.** `CustomTimeDilation` is per-actor: it slows
this character's tick and its animation, and nothing else. Global dilation would also slow the
camera — including the impact shake that's meant to be punctuating the same moment — which
undercuts the effect. It also has no cleanup hazard: if the actor is destroyed mid-stop, nothing is
left in a slowed state, whereas a global dilation with a dangling timer would freeze the whole game.

**Timers are immune to `CustomTimeDilation`.** The timer manager runs on world time, which
`CustomTimeDilation` doesn't affect (only global dilation does). So a 0.09s hit stop is exactly
0.09 real seconds. If it *were* affected, the stop would last `Duration / TimeDilation` — with the
defaults, 0.09 / 0.15 = **0.6 seconds**, nearly seven times too long. This is a real trap in
hand-rolled hit-stop implementations.

The clamp floor of `0.01f` prevents a zero dilation, which would freeze the actor permanently if
the timer were ever lost.

---

## 8. Combo component changes

### 8.1 Waking up a dead delegate

`OnAttackHitWindow` was declared in `UComboComponent` but never broadcast — nothing fired it. Since
the polish work needed an "the blade connects now" event, it got wired up:

```cpp
void UComboComponent::HandleComboWindowOpened(FName NotifyName, const FBranchingPointNotifyPayload& BranchingPointPayload)
{
    // The hit notify shares this delegate but is unrelated to the chain window — it marks the
    // frame the blade is meant to connect, which is what the camera and hit stop key off.
    if (NotifyName == AttackHitNotifyName)
    {
        OnAttackHitWindow.Broadcast(CurrentNode ? CurrentNode->NodeId : NAME_None);
        return;
    }

    if (NotifyName != ComboWindowNotifyName)
    {
        return;
    }
    ...
}
```

The name is configurable (`AttackHitNotifyName`, default `"AttackHit"`) for the same reason
`ComboWindowNotifyName` is.

`OnPlayMontageNotifyBegin` is a single delegate for *all* montage notifies, so this handler
dispatches on name. The early `return` after broadcasting is important — the hit notify must not
fall through into the combo-window logic and open a chain window.

This is also the correct hook for real hit detection when it arrives: the notify marks the frame
the blade is meant to connect, so a sweep test belongs on the same event.

### 8.2 Diagnostic removal

Three `UE_LOG(LogTemp, Warning, ...)` blocks were removed. They fired on every attack input, every
node entry, and every montage notify begin/end — several lines per swing. One was explicitly marked
`// TEMP ... remove once the aerial chaining bug is closed`, and per the project notes that bug is
closed. They'd also have made the new hit-notify path noisy.

---

## 9. Compile-safety notes (LWC)

**This code has not been compiled.** Running UBT with the editor open fails at the link stage, and
those failures are indistinguishable from real compile errors — so attempting it would have produced
misleading output rather than verification. Instead, every include, type, and API signature was
checked against the UE 5.8 engine headers directly, plus a static sweep for the failure mode that
bites hardest in UE5.

Since UE5's Large World Coordinates, `FVector`, `FRotator`, and `Velocity` components are **`double`**,
while gameplay floats stay `float`. `FMath::Max`, `FMath::Min`, and `FMath::Clamp` are same-type
templates:

```cpp
template<typename T> T Clamp(const T X, const T Min, const T Max);
```

so mixing `double` and `float` is a **template deduction failure — a hard compile error**, not a
silent narrowing. Every such call site was audited and explicitly cast:

```cpp
PeakFallSpeed = FMath::Max(PeakFallSpeed, static_cast<float>(-Movement->Velocity.Z));

const float GroundSpeed = static_cast<float>(GetVelocity().Size2D());
const float SpeedRatio = FMath::Clamp(GroundSpeed / FMath::Max(WalkSpeed, 1.f), 0.f, 1.f);

const float CurrentYaw = static_cast<float>(GetActorRotation().Yaw);
```

Comparisons (`double > float`) and non-template constructors (`FRotator(0.f, SomeDouble, 0.f)`) are
fine and were left alone.

One related change: `FSlashShakeChannel` was given an explicit two-argument constructor rather than
relying on aggregate initialisation (`{0.9f, 3.1f}`). `GENERATED_BODY()` should leave the struct an
aggregate, but that depends on macro expansion details it's not worth betting a compile round-trip
on.

Also verified statically: all declared methods have definitions in both new classes; no references
survive to the five deleted camera properties; `GFrameCounter` is reachable via `CoreMinimal.h` →
`CoreGlobals.h`; `TSubclassOf` via `ActorComponent.h`.

---

## 10. What is deliberately inert

Three things are written and wired but will not do anything until editor work is done. This is
intentional — they need hand edits in the editor, and leaving hooks in place means the editor
step is the *only* remaining step.

| Feature | Blocked on | Why C++ can't do it |
|---|---|---|
| Jump (all of it) | `IA_Jump` asset + `IMC_Gamepad` mapping | Needs an input action asset; no jump input is bound today |
| Impact shake + hit stop | `AttackHit` notify in the montages | Montage notify tracks are editor-only and not writable through the reflection API |
| Lean additive | AnimBP graph work | `GetLeanAmount` is exposed, but nothing consumes it |

The AnimBP was deliberately **not** reparented to a C++ `UAnimInstance`. That would have meant
rebuilding the working locomotion state machine by hand — anim state machines can't be authored
programmatically here — for no gain, since new variables are inert without state-machine consumers
anyway. `BlueprintPure` getters on the character give the AnimBP everything it needs via a
pawn-owner cast, with zero risk to the existing fall/land states.

---

## Appendix: tuning quick reference

All `EditDefaultsOnly` on `BP_SlashCharacter`.

| Category | Property | Default | What it does |
|---|---|---|---|
| Camera Profile (Sprint) | `ArmLength` / `FieldOfView` | 375 / 96 | How much the sprint opens up |
| Camera Profile (Combat) | `ArmLength` / `FieldOfView` | 255 / 78 | Attack close-in and focus |
| Camera Profile (any) | `BlendSpeed` | 4–9 | How fast that layer fades in/out |
| Camera Lag | `CameraLagMaxDistance` | 140 | Lower if combo dashes rubber-band |
| Camera Punch | `PitchDipStiffness` / `PitchDipDamping` | 240 / 22 | Landing bounce; lower damping = more ring |
| Camera Punch | `FOVPunchRecoverySpeed` | 6 | How fast the attack FOV kick recovers |
| Movement | `TurnInterpSpeed` / `SprintTurnInterpSpeed` | 14 / 6 | Pivot sharpness at rest vs. sprint |
| Combat | `CombatTurnInterpSpeed` | 3.5 | Attack steering authority |
| Movement | `LeanYawRateScale` | 180 | °/sec of yaw counting as full lean |
| Jump | `CoyoteTime` / `JumpBufferTime` | 0.12 / 0.15 | Input forgiveness windows |
| Jump | `JumpCutMultiplier` | 0.45 | Velocity kept on early release |
| Jump | `FallGravityScale` | 1.9 | Descent snappiness |
| Landing | `LandShakeMinSpeed` / `MaxSpeed` | 400 / 1400 | Impact response range |
| Landing | `LandMaxPitchDip` | 55 | Dip impulse at max fall speed |
| Combat | `AttackFOVPunch` | -3.5 | Negative narrows (focus); positive widens |
| Combat | `HitStopDuration` / `HitStopTimeDilation` | 0.09 / 0.15 | Impact freeze length and depth |
| Combo node | `CameraShakeScale` | 1.0 | Per-hit feedback multiplier |

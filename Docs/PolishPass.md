# AAA polish pass — 2026-07-28

Camera rig, movement feel, and combat feedback overhaul. Everything below is C++ unless it says
otherwise; the editor work at the bottom has to be done by hand.

## What changed in code

### New: `USlashCameraRigComponent` (`SlashCameraRigComponent.h/.cpp`)

Owns the whole third-person rig. The character pushes state in once per frame via `UpdateRig`;
nothing in the rig reaches back into the character, so it drops onto any pawn with a spring arm
and a camera.

- **Camera profiles.** `FSlashCameraProfile` bundles arm length, socket offset, target offset,
  FOV, and both lag speeds into one struct. Four of them — Default, Sprint, Combat, Air — are
  blended as *layers*, not as an exclusive state machine, so a sprinting player knocked into the
  air reads as a mix rather than a cut to whichever state won a priority test. This replaced a
  pile of independent `FInterpTo` calls in `ASlashCharacter::Tick` that could drift out of step.
- **Over-the-shoulder framing.** Default is a 300-unit boom at `SocketOffset (0, 55, 55)`. Sprint
  pulls back to 375 and drifts toward centre (shoulder framing reads as cramped at speed); combat
  tightens to 255 with a narrower 78° FOV.
- **Camera lag,** bounded at 140 units. Unbounded lag looks fine at a walk and then slingshots the
  moment a combo `LaunchCharacter` fires at 800 u/s. Rotation lag is **off** by default
  (`bEnableRotationLag`): the boom takes its rotation straight from the control rotation, so
  rotation lag delays the player's own look input, which reads as latency rather than weight.
- **Camera collision** enabled with a 12-unit probe, so the over-shoulder offset doesn't clip.
- **Additive springs on top of the profile:** an FOV punch that decays, and a damped pitch-dip
  spring for landings. The dip is a real spring, not a decay curve, so it overshoots slightly on
  the way back — that counter-swing is most of what sells the weight.

### New: `SlashCameraShake.h/.cpp`

The engine's oscillator shake patterns live in the **EngineCameras plugin**, so using them would
have meant a new module in `Slash.Build.cs` and a full rebuild instead of a Live Coding patch.
`USlashOscillatorShakePattern` covers the same ground against Engine-only API: per-axis
amplitude/frequency channels for location, rotation and FOV, sine or Perlin waveform, and its own
blend in/out (`UCameraShakeBase` passes the modifier alpha through but does not blend for you).

Three shakes derive from it: `USlashSprintCameraShake` (infinite, Perlin, deliberately subtle),
`USlashImpactCameraShake` and `USlashLandCameraShake` (short, sine, sharp).

The **hand-rolled sine bob** that used to write `CameraBoom->SocketOffset` every frame is gone. It
had to go regardless of taste: `SocketOffset` is also where the over-the-shoulder offset lives, so
the old `else` branch zeroing it would have snapped the camera to dead-centre the instant sprint
ended. The sprint shake replaces it, and the rig drives its `ShakeScale` from the sprint ramp so
amplitude fades with the sprint instead of popping on at the threshold.

### `ASlashCharacter`

- **Combo lockout relaxed.** `Move()` used to hard-return on `bIsInCombo`, which is the least
  AAA thing in the file. Translation still stays locked (adding input would fight the aerial
  combo's root motion, which drives the capsule itself), but the move direction is now recorded
  before that check so `UpdateFacing` can steer the swing toward the stick.
- **Speed-scaled turning.** Turn rate lerps from `TurnInterpSpeed` (14) to `SprintTurnInterpSpeed`
  (6) with the sprint ramp, so a sprint arcs instead of pivoting on a pin. Combat steering uses its
  own slower `CombatTurnInterpSpeed` (3.5).
- **Jump** — bound to a new optional `JumpAction`, with coyote time (0.12s, via the engine's own
  `CanJumpWhileFalling` hook), input buffering (0.15s), a jump cut on early release, and extra
  gravity on the descent only. Inert until you make `IA_Jump`; see below.
- **Landing feedback.** `Landed` fires a fall-speed-scaled shake plus a pitch dip, between 400 and
  1400 u/s of impact speed.
- **Hit stop.** `ApplyHitStop` uses `CustomTimeDilation` rather than global dilation, so the swing
  freezes without dragging the camera and the world into it, and the timer clearing it runs on
  undilated world time.
- **Anim hooks** — `GetSprintAlpha`, `IsSprinting`, `IsInCombo`, `GetLeanAmount` are
  `BlueprintPure`, so `ABP_SlashCharacter` can read them **without being reparented**. `LeanAmount`
  is derived from how fast the body actually turned this frame, scaled by ground speed, so
  pivoting on the spot doesn't lean into nothing.
- CMC retuned: separate braking friction, higher `MaxAcceleration`, air-control boost only near
  stationary so a standing jump steers but a sprinting leap keeps its arc.

### `UComboComponent` / `FComboNode`

- `AttackHitNotifyName` ("AttackHit") now broadcasts the previously-declared-but-never-fired
  `OnAttackHitWindow`. The character binds it to the impact shake and hit stop.
- `FComboNode::CameraShakeScale` (default 1.0) scales FOV kick, shake and hit stop per hit. Raise
  it on finishers. Existing `DA_Combo_Sword` entries pick up the default with no asset edit.
- Removed the temporary `[Combo]` diagnostic logs, which fired on every attack and notify.

## Editor work — required, in order

1. **Compile** (Live Coding, `Ctrl+Alt+F11`). One compile covers everything above; no `Build.cs`
   or `.uproject` change was needed.

2. **`IA_Jump`** — right-click in `/Game/Input` →
   Input → Input Action, name it `IA_Jump`, leave it Digital (bool). Add it to `IMC_Gamepad` on
   `Gamepad_FaceButton_Bottom`, then assign it to `BP_SlashCharacter`'s **Jump Action** slot.
   Until this exists, all jump code is dead — no jump input is bound today.

3. **Jump anim states** — `UAL1_StandardJump_Start` is currently unused (see the locomotion notes).
   With a jump input it becomes the missing third clip: add a `Jump_Start` state feeding `Fall`.
   Anim state machines have to be authored by hand.

4. **`AttackHit` notify** — in `AM_SwordComboRegular` and `AM_SwordComboAerial`, add an
   `AttackHit` notify on the frame the blade should connect in each section. Until then the impact
   shake and hit stop never fire. This is also the notify real hit detection should hang off.

5. **Lean additive (optional, best visual return)** — in `ABP_SlashCharacter`, cast the pawn owner
   to `SlashCharacter`, read `Get Lean Amount`, and drive a lean/roll additive over the locomotion
   state machine output but *before* the `DefaultSlot` node, so attacks aren't leaned.

6. **Check `BP_SlashCharacter`** after compiling. Five camera properties were removed from the
   character (`SprintArmLengthOffset`, `SprintFOVOffset`, `CameraInterpSpeed`, `SprintBobAmplitude`,
   `SprintBobFrequency`) — they now live on the rig's profiles. Any values overridden in the
   Blueprint for those will drop with a load warning; that is expected.

## Tuning

Everything is `EditDefaultsOnly` on `BP_SlashCharacter`. The knobs most worth a pass in PIE:

| Where | Property | Default | Effect |
|---|---|---|---|
| Camera Rig → Profiles | `SprintProfile.ArmLength` / `FieldOfView` | 375 / 96 | How much the sprint "opens up" |
| Camera Rig → Profiles | `CombatProfile.ArmLength` | 255 | Attack close-in |
| Camera Rig → Lag | `CameraLagMaxDistance` | 140 | Lower if combo dashes rubber-band |
| Character → Movement | `TurnInterpSpeed` / `SprintTurnInterpSpeed` | 14 / 6 | Pivot sharpness |
| Character → Combat | `CombatTurnInterpSpeed` | 3.5 | Attack steering authority |
| Character → Jump | `FallGravityScale` | 1.9 | Descent snappiness |
| Character → Combat | `HitStopDuration` / `HitStopTimeDilation` | 0.09 / 0.15 | Impact freeze |

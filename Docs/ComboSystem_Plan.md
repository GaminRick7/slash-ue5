# Modular Combo System — Design Plan

Status: **design only, not implemented**. Current combo logic lives directly in
`SlashCharacter.h/.cpp` (`ComboIndex`, `bComboWindowOpen`, hardcoded
`AttackMontage`, `Attack%d` section names, `ApplyAttackDash`). This document
describes the replacement system before any code is written.

## Goals

- **Reusable**: combo state lives on an `ActorComponent`, not baked into
  `ASlashCharacter`, so any actor (player, enemies) can use it.
- **Data-driven**: designers define the combo as a data asset — no
  recompiling to add/rebalance a hit.
- **Input-buffered**: an attack press slightly before the combo window opens
  is queued and consumed automatically, instead of being dropped.
- **Decoupled from damage**: the system exposes hook events
  (`OnComboNodeEntered`, `OnAttackHitWindow`, `OnComboEnded`, etc.); hit
  detection and damage application are a separate system that listens to
  these, built later.
- **Branchable later**: the node/transition model supports a branching tree
  (e.g. a Heavy finisher off Light-Light), but see Non-goals — there's only
  one attack input (`IA_Attack`) today, so v1 is a straight chain of nodes,
  each with at most one outgoing transition.

## Non-goals (for this pass)

- Hit detection / damage application (hooks only).
- A custom graph editor UI. The tree is authored as an array of structs in a
  `UDataAsset`, editable in the normal Details panel.
- AI/enemy-specific decision logic for *which* combo node to pick — enemies
  would drive the same component via the same public API a player controller
  uses.
- Actual branching content. Nothing in the project has a second attack input
  yet, so `FComboTransition` exists in the model but every node will have
  zero or one transition until a Heavy action (or similar) is added.

## Core Data Model

### `FComboNode` (struct)

One node = one attack in the tree (one montage section).

```
FName NodeId;                     // unique key, e.g. "Light_1", "Light_2_Heavy_Finisher"
UAnimMontage* Montage;             // which montage this section lives in
FName SectionName;                 // section to jump to within that montage
float PlayRate = 1.f;

// Movement
float DashSpeed = 0.f;             // applied on entering this node

// Branching
TArray<FComboTransition> Transitions;  // see below

// Hooks (see Delegates section) fire with this node's NodeId as context
```

Combo window notify name and input buffer seconds are **component-level**
settings, not per-node — the project already conventions on a single
`"ComboWindow"` notify state, and one global buffer window is enough until
there's a concrete reason to vary it per hit.

### `FComboTransition` (struct)

An edge out of a node.

```
UInputAction* TriggerAction;   // e.g. AttackAction
FName NextNodeId;              // which FComboNode to jump to
```

A node with zero transitions is a combo **finisher** — pressing attack while
on it (or letting the window close) ends the combo back to idle.

### `UComboTreeAsset` (UDataAsset)

```
TArray<FComboNode> Nodes;
FName EntryNodeId;              // which node a fresh combo starts on
```

One asset per "moveset" — e.g. `DA_Combo_Sword`, `DA_Combo_Unarmed`.
Swapping the equipped weapon = swapping which `UComboTreeAsset` the
component is pointed at, no code change.

Why a `UDataAsset` and not a `DataTable`: nodes hold hard/soft object
references (montages) and nested structs (`Transitions`) comfortably in a
Details-panel array; DataTables get awkward with nested arrays-of-structs and
don't give per-row Blueprint override points as cleanly.

## Runtime Component: `UComboComponent`

`UActorComponent`, added to any `ACharacter` that wants combos.

### State

```
UComboTreeAsset* ActiveTree;
FComboNode* CurrentNode;         // null when not in a combo
bool bWindowOpen;
bool bHasBufferedInput;
UInputAction* BufferedAction;    // buffer depth 1 — a second press while
                                  // one is already buffered is dropped
float TimeSinceWindowOpened;     // for buffer expiry
```

### Public API

```
void SetActiveTree(UComboTreeAsset* NewTree);   // swap moveset (weapon change)
void RequestAttack(UInputAction* Action);        // called from the owner's input handling
void CancelCombo();                              // e.g. on hit-stagger, dodge
```

### Delegates (the "hooks" for other systems)

```
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnComboNodeEvent, FName, NodeId, bool, bIsComboStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnComboHitWindow, FName, NodeId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnComboEnded, FName, NodeId, bool, bWasInterrupted);

FOnComboNodeEvent OnComboNodeEntered;  // fires every hit; bIsComboStart is true only on the first
FOnComboHitWindow OnAttackHitWindow;   // fires alongside a "damage window" notify, for a future hit-detection system to bind to
FOnComboEnded     OnComboEnded;
```

The owning character binds `OnComboNodeEntered` to trigger its dash impulse
(replacing the current `ApplyAttackDash` call sites) and to lock movement
when `bIsComboStart` is true, and binds `OnComboEnded` to unlock it
(replacing the `ComboIndex != 0` check in `Move()`).

### Flow

1. `RequestAttack(Action)` called (from Enhanced Input, same as today).
2. If `CurrentNode == nullptr`: enter `ActiveTree->EntryNodeId`.
3. If `CurrentNode` is set and `bWindowOpen`: look up `CurrentNode->Transitions`
   for one matching `Action`. If found, jump to `NextNodeId`. If not found,
   input is simply ignored (wrong button for this branch).
4. If `CurrentNode` is set and **not** `bWindowOpen`: don't discard yet —
   store `Action` in `BufferedAction` with a timestamp (overwriting nothing,
   since buffer depth is 1). When the window later opens (via the montage
   notify), immediately re-run step 3 using the buffered action before
   accepting new live input. If the window opens with nothing buffered, and
   nothing arrives before the window closes, the combo ends naturally
   (mirrors today's `HandleComboWindowClosed` behavior).
5. Montage notifies (component's `ComboWindowNotifyName` begin/end,
   generalized version of today's hardcoded `"ComboWindow"`) drive
   `bWindowOpen` and call back into the component the same way
   `HandleComboWindowOpened` / `HandleComboWindowClosed` do today.
6. On montage end with no further transition taken: fire `OnComboEnded`,
   clear `CurrentNode`.

This is the same shape as the existing `RequestAttack` /
`HandleComboWindowOpened` / `HandleComboWindowClosed` /
`HandleAttackMontageFinished` quartet in `SlashCharacter.cpp` today — the
migration is mostly "generalize the hardcoded bits into node lookups," not a
rewrite of the control flow.

## Input Mapping

The owning character still owns its `UInputAction*` bindings (Enhanced
Input), same as today's `SprintAction`/`AttackAction`. It forwards each
attack-capable action straight into `ComboComponent->RequestAttack(Action)`.
The component never binds input directly — this keeps it engine-input-system
agnostic and reusable for AI-driven actors that call `RequestAttack` with a
"virtual" action reference instead of a real key press.

## Migration Path from Current Code

| Today (`SlashCharacter`)                          | Becomes                                              |
|-----------------------------------------------------|-------------------------------------------------------|
| `AttackMontage` (single `UAnimMontage*`)             | `UComboTreeAsset` referencing possibly multiple montages |
| `ComboIndex` (int)                                   | `CurrentNode` (`FComboNode*`) on the component        |
| `"Attack%d"` string formatting                       | `FComboNode::SectionName`, authored per node          |
| `bComboWindowOpen` / `bComboWindowConsumed`          | Component-internal `bWindowOpen` + buffer state       |
| Hardcoded `"ComboWindow"` notify name                | Component-level `ComboWindowNotifyName` setting (still likely `"ComboWindow"` in practice, just no longer a magic string) |
| `ApplyAttackDash()` reading `ComboIndex`             | Character binds `OnComboNodeEntered`, reads `DashSpeed` off the node instead of `AttackDashSpeed + Growth * (ComboIndex - 1)` |
| `Move()` checking `ComboIndex != 0`                  | `Move()` checks a `bIsInCombo` bool the character caches from `OnComboNodeEntered`/`OnComboEnded` |

## Future Work (stretch, post-v1)

- A second attack action (Heavy) and real transitions/branches once one
  exists — this is what `FComboTransition` is for.
- Visual node-graph editor (`EdGraph`-based, similar to how AnimBP state
  machines are authored) instead of an array-of-structs.
- Per-node hit-stop / hitstun tuning.
- Directional input on nodes (e.g. forward+attack = different finisher).
- A separate, earlier "cancel window" (cancel into a dodge/dash) distinct
  from the combo window — real character-action games often split these;
  not needed until dodge/dash exists.
- Difficulty/character-specific combo trees selected at runtime for
  enemies vs. player.

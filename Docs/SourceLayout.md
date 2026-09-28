# Source layout

`Source/Slash` was reorganised from a flat directory into feature folders on 2026-08-01.

```
Source/Slash/
  Slash.Build.cs                      module rules — must stay at the module root
  Slash.h / Slash.cpp                 module implementation
  Character/
    SlashCharacter.h / .cpp
  Combat/
    ComboComponent.h / .cpp
    ComboTreeAsset.h / .cpp
  Camera/
    SlashCameraRigComponent.h / .cpp
    SlashCameraShake.h / .cpp
  Framework/
    SlashGameMode.h / .cpp
    SlashPlayerController.h / .cpp
  Targeting/                          empty — Phase 1 of TargetingSystem_Spec.md lands here
```

No `Public/`/`Private/` split: nothing outside this module includes its headers, so the split would
be ceremony. Add it if a second module or plugin ever needs to link against Slash.

## Include convention

**This layout requires one line in `Slash.Build.cs`:**

```csharp
PublicIncludePaths.Add(ModuleDirectory);
```

Without it, only same-folder includes resolve, and cross-folder ones fail with
`'Character/SlashCharacter.h' file not found`. UBT does **not** put a flat module's root on the
include path by itself — the old single-directory layout worked only because a quoted include
searches the including file's own directory first, and every file shared one. This was verified the
hard way: the first build after the move failed on exactly that, and adding the line fixed it.

With it in place, **cross-folder includes are written relative to the module root**:

```cpp
#include "Combat/ComboComponent.h"
#include "Camera/SlashCameraRigComponent.h"
```

Same-folder includes stay bare (`#include "ComboTreeAsset.h"`) — quoted includes resolve against the
including file's own directory first.

`*.generated.h` includes are unaffected by any of this: UHT keys them to the header's *filename*,
never its path, so `#include "SlashCharacter.generated.h"` stays correct wherever the file lives.

### One hazard worth knowing

`Camera/` collides by name with the engine's `Camera/` include prefix — `SlashCharacter.cpp` has
both `#include "Camera/CameraComponent.h"` (engine) and `#include "Camera/SlashCameraRigComponent.h"`
(ours). That works today because no filename appears in both. **Never add a file to
`Source/Slash/Camera/` whose name matches an engine `Camera/` header** (`CameraComponent.h`,
`PlayerCameraManager.h`, `CameraShakeBase.h`, …) — it would shadow the engine's and the error would
point somewhere unrelated. Same caution applies to `Components/`, `Animation/`, `GameFramework/` if
those folders are ever added.

## After moving files

File moves cannot be applied by Live Coding. The sequence is: close the editor → regenerate project
files → full rebuild. Blueprint references are unaffected — they bind to the module and class name,
not the file path, so `BP_SlashCharacter` and friends survive the move untouched.

## Verified

`Build.sh SlashEditor Mac Development` succeeded on 2026-08-01 after the move — all 10 translation
units compiled, linked and deployed. The layout is known-good, not just plausible.

## Rollback

Flattening again is `mv` back to `Source/Slash/`, reverting the `PublicIncludePaths` line in
`Slash.Build.cs`, plus reverting four include lines:

| File | Line to revert |
|---|---|
| `Character/SlashCharacter.cpp` | `"Camera/SlashCameraRigComponent.h"` → `"SlashCameraRigComponent.h"` |
| `Character/SlashCharacter.cpp` | `"Combat/ComboComponent.h"` → `"ComboComponent.h"` |
| `Character/SlashCharacter.cpp` | `"Combat/ComboTreeAsset.h"` → `"ComboTreeAsset.h"` |
| `Framework/SlashGameMode.cpp` | `"Character/SlashCharacter.h"` → `"SlashCharacter.h"` |

Nothing else changed — no file contents beyond those four lines, no renames, no deletions.

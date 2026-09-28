# L_Arena_Combat generator

The town in `L_Arena_Combat` was laid out by these planning scripts and built from the Medieval Village kit in the editor.

- `plan_street.py` — walkable outline `P` (CCW polygon) and the street-facing house rows → `plan.json`; `H.json` is the final list `[cx, cy, yaw, W, D, floors, seed, awning, tower]`.
- `plan_backdrop.py` — second ring of skyline houses → `B.json` `[cx, cy, yaw, W, D, floors, seed]`.
- `merged_collision.py` — sets the merged village meshes to complex-as-simple collision (run in the editor with `py`).

Kit facts: 2 m grid, 3 m storeys; wall pieces face **out along local +Y**; house front is local -Y (front direction `(sin h, -cos h)`).
Each house was merged afterwards (Merge Actors) into `/Game/Environment/Merged/SM_SM_House_*` / `SM_SM_Backdrop_*`, and the cobbles into `SM_SM_TownGround`.

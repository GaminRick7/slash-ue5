# The merged village meshes (Merge Actors) come out with no simple collision shapes, so
# anything that sweeps a shape — the camera spring arm's probe, a character capsule — passes straight
# through them; only complex line traces hit. Make them use their render geometry as collision.
# Run in the editor (not during PIE): py "<project>/Tools/LevelGen/merged_collision.py"
import unreal
reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path("/Game/Environment/Merged", recursive=True)
changed = 0
for data in assets:
    mesh = data.get_asset()
    if not isinstance(mesh, unreal.StaticMesh):
        continue
    body = mesh.get_editor_property("body_setup")
    if body is None:
        continue
    if body.get_editor_property("collision_trace_flag") != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        mesh.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
        changed += 1
unreal.log("[MergedCollision] %d of %d meshes set to complex-as-simple" % (changed, len(assets)))

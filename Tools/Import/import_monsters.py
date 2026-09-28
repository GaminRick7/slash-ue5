import unreal
SRC = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()) + "Bestiary - Dungeon Monsters Kit[Standard]/Exports/GLB (Godot-Unreal)/"
DST = "/Game/Characters/Monsters"
PIPE = DST + "/Import/PL_MonsterGLTF"
at = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
if not eal.does_asset_exist(PIPE):
    eal.duplicate_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline", PIPE)
pipe = eal.load_asset(PIPE)
skel = eal.load_asset("/Game/Animations/UAL1_Standard/SkeletalMeshes/UAL1_Standard_Skeleton")
common = pipe.get_editor_property("common_skeletal_meshes_and_animations_properties")
common.set_editor_property("skeleton", skel)
eal.save_asset(PIPE)
unreal.log("[MonsterImport] pipeline skeleton=%s" % common.get_editor_property("skeleton"))
mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
for name in ["Imp", "Puglin"]:
    src = unreal.InterchangeManager.create_source_data(SRC + name + ".glb")
    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.override_pipelines = [unreal.SoftObjectPath(PIPE + ".PL_MonsterGLTF"), unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline")]
    ok = mgr.import_asset(DST + "/" + name, src, params)
    unreal.log("[MonsterImport] %s -> %s" % (name, ok))

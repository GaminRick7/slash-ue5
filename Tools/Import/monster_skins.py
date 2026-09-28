# Imports the Bestiary pack's alternate colour textures and builds one material instance per skin.
# Run in the editor: py "<project>/Tools/Import/monster_skins.py"
import unreal
SRC = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()) + "Bestiary - Dungeon Monsters Kit[Standard]/Textures/"
M = "/Game/Characters/Monsters/"
EYE_GLOW = 8.0
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()

SKINS = {
    "Imp": (M + "Imp/Imp/", "MI_Imp", {"Green": "T_Imp_BaseColor_2", "Blue": "T_Imp_BaseColor_3"}),
    "Puglin": (M + "Puglin/Puglin/", "MI_Puglin", {"Red": "T_Puglin_BaseColor_2", "Teal": "T_Puglin_BaseColor_3"}),
}
tasks = []
for name, (root, base, skins) in SKINS.items():
    for tex in skins.values():
        if not eal.does_asset_exist(root + "Textures/" + tex):
            t = unreal.AssetImportTask()
            t.filename = SRC + tex + ".png"
            t.destination_path = root + "Textures"
            t.automated = True
            t.save = True
            tasks.append(t)
if tasks:
    at.import_asset_tasks(tasks)

for name, (root, base, skins) in SKINS.items():
    base_path = root + "Materials/" + base
    mel.set_material_instance_scalar_parameter_value(eal.load_asset(base_path), "EmissiveStrength", EYE_GLOW)
    eal.save_asset(base_path)
    for skin, tex in skins.items():
        mi_path = root + "Materials/" + base + "_" + skin
        if not eal.does_asset_exist(mi_path):
            eal.duplicate_asset(base_path, mi_path)
        mi = eal.load_asset(mi_path)
        texture = eal.load_asset(root + "Textures/" + tex)
        mel.set_material_instance_texture_parameter_value(mi, "BaseColorTexture", texture)
        mel.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", EYE_GLOW)
        eal.save_asset(mi_path)
        unreal.log("[MonsterSkins] %s -> %s (%s)" % (mi_path, tex, texture is not None))

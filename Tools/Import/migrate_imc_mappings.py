# UE 5.7 moved UInputMappingContext's key list from the deprecated `Mappings` array into
# `DefaultKeyMappings.Mappings`. The engine only migrates assets saved before that change (in PostLoad),
# so contexts created in 5.8 and filled through the old field have no live mappings at all.
# This moves them across, keeping the same modifier/trigger sub-objects, and empties the old field.
# Run in the editor: py "<project>/Tools/Import/migrate_imc_mappings.py"
import unreal
for name in ["IMC_GamepadExtras", "IMC_KeyboardMouse", "IMC_Dodge"]:
    path = "/Game/Input/" + name
    imc = unreal.load_asset(path)
    old = list(imc.get_editor_property("mappings"))
    data = imc.get_editor_property("default_key_mappings")
    current = list(data.get_editor_property("mappings"))
    if old and not current:
        data.set_editor_property("mappings", old)
        imc.set_editor_property("default_key_mappings", data)
    if old:
        imc.set_editor_property("mappings", [])
    unreal.EditorAssetLibrary.save_asset(path)
    live = imc.get_editor_property("default_key_mappings").get_editor_property("mappings")
    unreal.log("[IMCMigrate] %s live=%d %s" % (name, len(live), [(m.action.get_name(), m.key.export_text(), len(m.modifiers), len(m.triggers)) for m in live]))

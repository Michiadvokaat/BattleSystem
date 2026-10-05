"""Creates the see-through material of the LevelDesigner's unit ghost (/Game/Combat/M_DesignGhost).

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateDesignGhostMaterial.py" -unattended -nullrhi -nosplash

Translucent and unlit: the vector parameter Color (the team color, set per ghost) lights it, brighter at the edges
(Fresnel), and the scalar parameter Opacity (UCombatSettings::DesignGhostOpacity) sets how see-through it is. An
existing material is rebuilt. It is referenced by UCombatSettings::DesignGhostMaterial.
"""

import unreal

LOG_TAG = "[DesignGhost]"
ASSET_PATH = "/Game/Combat"
ASSET_NAME = "M_DesignGhost"
EDGE_BOOST = 1.5


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def main():
    full_path = f"{ASSET_PATH}/{ASSET_NAME}"
    lib = unreal.MaterialEditingLibrary
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        material = unreal.load_asset(full_path)
        lib.delete_all_material_expressions(material)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ASSET_NAME, ASSET_PATH, unreal.Material, unreal.MaterialFactoryNew())
        if not material:
            raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Skeletal meshes (the modular looks) and static meshes (props, the placeholder body) both use it.
    material.set_editor_property("used_with_skeletal_mesh", True)

    color = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.2, 0.6, 1.0, 1.0))

    fresnel = lib.create_material_expression(material, unreal.MaterialExpressionFresnel, -600, 200)
    boost = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -600, 320)
    boost.set_editor_property("r", EDGE_BOOST)
    edge = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 220)
    lib.connect_material_expressions(fresnel, "", edge, "A")
    lib.connect_material_expressions(boost, "", edge, "B")
    one = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 340)
    one.set_editor_property("r", 1.0)
    brightness = lib.create_material_expression(material, unreal.MaterialExpressionAdd, -250, 260)
    lib.connect_material_expressions(edge, "", brightness, "A")
    lib.connect_material_expressions(one, "", brightness, "B")
    emissive = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -150, 0)
    lib.connect_material_expressions(color, "", emissive, "A")
    lib.connect_material_expressions(brightness, "", emissive, "B")
    lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 450)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 0.45)
    lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {full_path}")
    log(f"Saved {full_path}")


main()

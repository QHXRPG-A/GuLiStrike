import unreal


# 闪现特效资源的统一保存目录。
FX_PATH = "/Game/Variant_TwinStick/FX"


def create_material(name, tint, opacity, heatwave):
    """创建一个半透明无光照材质；heatwave 为 True 时额外加入折射热波。"""
    asset_path = f"{FX_PATH}/{name}"
    # 防止重复运行脚本时覆盖已经调好的材质。
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        raise RuntimeError(f"Asset already exists: {asset_path}")

    # 新建材质，并配置成适合特效的双面、半透明、无光照模式。
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name,
        FX_PATH,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    # Tint 是运行时动态材质实例可修改的发光颜色参数。
    color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -500, -80
    )
    color.set_editor_property("parameter_name", "Tint")
    color.set_editor_property("default_value", unreal.LinearColor(*tint, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    # Opacity 是运行时随生命周期递减的透明度参数。
    opacity_parameter = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -500, 120
    )
    opacity_parameter.set_editor_property("parameter_name", "Opacity")
    opacity_parameter.set_editor_property("default_value", opacity)

    opacity_expression = opacity_parameter
    if heatwave:
        # 热波只保留模型边缘：Fresnel 与透明度相乘后形成扩散的环状轮廓。
        fresnel = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionFresnel, -260, 160
        )
        fresnel.set_editor_property("exponent", 2.5)
        opacity_expression = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionMultiply, -30, 120
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            opacity_parameter, "", opacity_expression, "A"
        )
        unreal.MaterialEditingLibrary.connect_material_expressions(
            fresnel, "", opacity_expression, "B"
        )

        # 轻微折射场景画面，产生空气受热后的空间扭曲感。
        refraction = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant, -240, 300
        )
        refraction.set_editor_property("r", 1.025)
        unreal.MaterialEditingLibrary.connect_material_property(
            refraction, "", unreal.MaterialProperty.MP_REFRACTION
        )

    # 将最终透明度接入材质输出，编译并保存资源。
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity_expression, "", unreal.MaterialProperty.MP_OPACITY
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log(f"Created {asset_path}")


# 残影使用纯半透明发光材质；热波使用带菲涅耳和折射的材质。
create_material("M_BlinkAfterimage", (0.08, 0.62, 1.0), 0.55, False)
create_material("M_BlinkHeatwave", (0.15, 0.85, 1.0), 0.38, True)
unreal.log("Blink VFX material creation complete")

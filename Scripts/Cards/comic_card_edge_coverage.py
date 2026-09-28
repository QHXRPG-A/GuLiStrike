"""The comic illustration stays under the fixed full-card frame aperture."""
import unreal

# The UI frame starts at U=.052/.948 and V=.050/.765. Keep a small underlap
# so MSAA/TAA and the .035-cm face/frame spacing cannot uncover the background.
ARTWORK_CLIP_CODE='return step(0.045,UV.x)*step(UV.x,0.955)*step(0.043,UV.y)*step(UV.y,0.772);'
SPEED_COVERAGE_LAYOUTS={
    1:((0.,-.0875),(1.16,.745)),
    2:((0.,.025),(1.20,.745)),
    4:((-.066,-.039),(1.18,.9555)),
    5:((0.,-.0875),(1.32,.96)),
}

def configure_artwork_clip(material):
    mel=unreal.MaterialEditingLibrary
    nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==material]
    uv=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionTextureCoordinate) and str(n.get_editor_property('desc'))=='WM_LAYER_-7200_-1000')
    guard=next((n for n in nodes if str(n.get_editor_property('desc'))=='WM_FIXED_FRAME_APERTURE'),None)
    if guard is None:
        guard=mel.create_material_expression(material,unreal.MaterialExpressionCustom,-4700,3300)
        guard.set_editor_property('desc','WM_FIXED_FRAME_APERTURE')
        entry=unreal.CustomInput()
        entry.set_editor_property('input_name','UV')
        guard.set_editor_property('inputs',[entry])
        guard.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    guard.set_editor_property('code',ARTWORK_CLIP_CODE)
    assert mel.connect_material_expressions(uv,'',guard,'UV')
    assert mel.connect_material_property(guard,'',unreal.MaterialProperty.MP_OPACITY_MASK)
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    mel.recompile_material(material)
    return guard

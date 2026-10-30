"""Create the owned rigid six-layer shader. Source art and parameters remain Excel-owned."""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/BurningRogue_20261029'
SOURCE='/Game/GuLiStrike/Cards/GroundMech/Ricochet/Materials/M_Ricochet_RegisteredLayers_Builtin_v1'
DEST='/Game/GuLiStrike/CardSystem/RevealDemo/Materials/M_CardArtwork_RegisteredRigid_v1'

def install_rigid_sampling(parent):
    edit=unreal.MaterialEditingLibrary
    nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==parent]
    def node(cls,desc):
        n=next((n for n in nodes if isinstance(n,cls) and str(n.get_editor_property('desc'))==desc),None)
        if n is None:
            n=edit.create_material_expression(parent,cls,-7000,-1400)
            n.set_editor_property('desc',desc);nodes.append(n)
        return n
    cam=node(unreal.MaterialExpressionCameraPositionWS,'RIGID_CAMERA_CENTER')
    pivot=node(unreal.MaterialExpressionObjectPositionWS,'RIGID_CARD_CENTER')
    delta=node(unreal.MaterialExpressionSubtract,'RIGID_VIEW_DELTA')
    tangent=node(unreal.MaterialExpressionTransform,'RIGID_VIEW_TANGENT')
    tangent.set_editor_property('transform_source_type',unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    tangent.set_editor_property('transform_type',unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    assert edit.connect_material_expressions(cam,'',delta,'A')
    assert edit.connect_material_expressions(pivot,'',delta,'B')
    assert edit.connect_material_expressions(delta,'',tangent,'Input') or edit.connect_material_expressions(delta,'',tangent,'')
    uv=node(unreal.MaterialExpressionTextureCoordinate,'RIGID_UNSHIFTED_UV')
    global_depth=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))=='Layers global depth')
    for i in range(1,7):
        depth=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))==f'Layer {i} Depth')
        shifted=node(unreal.MaterialExpressionCustom,f'RIGID_LAYER_{i}_UV')
        shifted.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT2)
        inputs=[]
        for name in ['UV','Camera','Depth','GlobalDepth']:
            item=unreal.CustomInput();item.set_editor_property('input_name',name);inputs.append(item)
        shifted.set_editor_property('inputs',inputs)
        shifted.set_editor_property('code','return UV.xy+clamp(Camera.xy/max(abs(Camera.z),0.05),-1.0,1.0)*Depth*GlobalDepth;')
        for a,p in [(uv,'UV'),(tangent,'Camera'),(depth,'Depth'),(global_depth,'GlobalDepth')]:
            assert edit.connect_material_expressions(a,'',shifted,p)
        bounded=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionCustom) and str(n.get_editor_property('desc'))==f'WM_LAYER_{i}_BoundedUV')
        assert edit.connect_material_expressions(shifted,'',bounded,'UV')
    for n in nodes:
        if isinstance(n,unreal.MaterialExpressionCustom) and 'PinnedUV' in str(n.get_editor_property('desc')):n.set_editor_property('code','return ShiftedUV.xy;')
    # The foreground ownership mask follows the exact same rigid source UV.
    bounded=next(n for n in nodes if isinstance(n,unreal.MaterialExpressionCustom) and str(n.get_editor_property('desc'))=='WM_LAYER_1_BoundedUV')
    mask=node(unreal.MaterialExpressionComponentMask,'RIGID_FOREGROUND_MASK_UV')
    for k,v in [('r',True),('g',True),('b',False),('a',False)]:mask.set_editor_property(k,v)
    assert edit.connect_material_expressions(bounded,'',mask,'Input') or edit.connect_material_expressions(bounded,'',mask,'')
    for n in nodes:
        if isinstance(n,unreal.MaterialExpressionTextureSampleParameter2D) and str(n.get_editor_property('parameter_name'))=='Foreground Front Mask':
            assert edit.connect_material_expressions(mask,'',n,'UVs')

def main():
    assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,'Do not edit assets during PIE'
    dirty={str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert SOURCE not in dirty,'Preserve unsaved source card material edits'
    if DEST in dirty:
        owned=unreal.load_asset(DEST)
        assert any(n.get_outer()==owned and str(n.get_editor_property('desc'))=='RIGID_CAMERA_CENTER' for n in unreal.ObjectIterator(unreal.MaterialExpression)),'Preserve unsaved destination edits'
    parent=unreal.load_asset(DEST) or unreal.EditorAssetLibrary.duplicate_asset(SOURCE,DEST)
    assert isinstance(parent,unreal.Material)
    install_rigid_sampling(parent)
    edit=unreal.MaterialEditingLibrary
    nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==parent]
    customs={str(n.get_editor_property('desc')):n for n in nodes if isinstance(n,unreal.MaterialExpressionCustom)}
    # Every sample receives one constant layer displacement. Never pin/warp image edges.
    for desc,node in customs.items():
        if 'PinnedUV' in desc:node.set_editor_property('code','return ShiftedUV.xy;')
    composite=customs['WM_LAYER_Composite']
    def parameter(cls,name,value):
        obj=next((n for n in nodes if isinstance(n,cls) and str(n.get_editor_property('parameter_name'))==name),None)
        if obj is None:obj=edit.create_material_expression(parent,cls,-5200,-1200)
        obj.set_editor_property('parameter_name',name);obj.set_editor_property('default_value',value);return obj
    ring=parameter(unreal.MaterialExpressionScalarParameter,'Ring Occlusion Enabled',0.0)
    plane=parameter(unreal.MaterialExpressionVectorParameter,'Ring Occlusion Plane',unreal.LinearColor(.13,393.7,0,0))
    inputs=list(composite.get_editor_property('inputs'));names={str(i.get_editor_property('input_name')) for i in inputs}
    for name in ['RingEnabled','RingPlane','RingUV']:
        if name not in names:
            item=unreal.CustomInput();item.set_editor_property('input_name',name);inputs.append(item)
    composite.set_editor_property('inputs',inputs)
    assert edit.connect_material_expressions(ring,'',composite,'RingEnabled')
    assert edit.connect_material_expressions(plane,'',composite,'RingPlane')
    assert edit.connect_material_expressions(customs['WM_LAYER_3_BoundedUV'],'',composite,'RingUV')
    composite.set_editor_property('code','''float3 c=Color5;
c=lerp(c,Color4,Alpha4);
float2 rp=RingUV.xy*1536.0-256.0;
float front=step(RingPlane.x*rp.x+RingPlane.y,rp.y);
float nearPart=lerp(1.0-saturate(Behind),front,saturate(RingEnabled));
c=lerp(c,Color2,Alpha2*(1.0-nearPart));
c=lerp(c,Color3,Alpha3);
c=lerp(c,Color2,Alpha2*nearPart);
c=lerp(c,Color1,Alpha1);
c=lerp(c,Color0,Alpha0*(1.0-saturate(NearBehind)*Alpha3));
return c;''')
    edit.recompile_material(parent)
    assert unreal.EditorAssetLibrary.save_loaded_asset(parent,False)
    # The existing Ground-only parent also stops deforming its foreground object.
    original=unreal.load_asset(SOURCE)
    install_rigid_sampling(original)
    for node in unreal.ObjectIterator(unreal.MaterialExpressionCustom):
        if node.get_outer()==original and 'PinnedUV' in str(node.get_editor_property('desc')):
            node.set_editor_property('code','return ShiftedUV.xy;')
    edit.recompile_material(original);assert unreal.EditorAssetLibrary.save_loaded_asset(original,False)
    report={'success':True,'material':parent.get_path_name(),'rigid_translation':True,'complete_ring_single_sample':True,'motion_binding':'Excel Layers.MotionOwnerLayer resolves a shared layer depth','material_diagnostics':str(unreal.MaterialNodeService.get_material_diagnostics(DEST)),'source_ground_parent_diagnostics':str(unreal.MaterialNodeService.get_material_diagnostics(SOURCE)),'assistant_visual_test':False}
    (OUT/'rigid_material_authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report))
try:main()
except Exception:print(json.dumps({'success':False,'error':traceback.format_exc()}))

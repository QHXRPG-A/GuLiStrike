"""Faction color candidates from the actual frozen B_v1 geometry, never UE assets."""
import bpy,json,hashlib,math
import numpy as np
from pathlib import Path
from mathutils import Vector,Matrix

R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
O=R/'TeamPalette_B_v2_20261007';T=O/'Textures';T.mkdir(exist_ok=True)
SOURCE=R/'Production_B_v1/SSF_Production_B_v1.blend'
OUT=O/'SSF_TeamPalette_B_v2.blend'
assert not OUT.exists(),'Existing candidate must be revised under a new version'
assert Path(bpy.data.filepath).resolve()==SOURCE.resolve()
source_hash=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
assert source_hash=='6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e'
construction=json.loads((R/'Production_B_v1/construction_report.json').read_text(encoding='utf8'))
reference=json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))
refs={a['key']:a for a in reference['assets']};assets={a['key']:a for a in construction['assets']}
KEYS=['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter']
ROLES=['Primary','Equipment','Accent','Frame']
# Assign source mechanical regions to the two requested existing color families.
# Blue AirBase and Red CommandCenter are exact palette anchors. Other buildings
# keep their old region boundaries, with light braces and functional accents.
BLUE={
 'AirBase':['#EE9D58','#274E61','#FEE4D9','#1A182F'],
 'CloningCenter':['#FEE4D9','#274E61','#EE9D58','#1A182F'],
 'CommandCenter':['#274E61','#FEE4D9','#EE9D58','#1A182F'],
 'MilitaryFactory':['#274E61','#EE9D58','#FEE4D9','#274E61'],
 'Reactor':['#274E61','#FEE4D9','#EE9D58','#1A182F'],
 'StrategyCenter':['#274E61','#FEE4D9','#EE9D58','#274E61']}
RED={
 'AirBase':['#EE9D58','#A34053','#E3B6B1','#662249'],
 'CloningCenter':['#E3B6B1','#A34053','#EE9D58','#662249'],
 'CommandCenter':['#A34053','#E3B6B1','#EE9D58','#662249'],
 'MilitaryFactory':['#A34053','#E3B6B1','#EE9D58','#A34053'],
 'Reactor':['#A34053','#E3B6B1','#EE9D58','#662249'],
 'StrategyCenter':['#A34053','#E3B6B1','#EE9D58','#A34053']}

def srgb(code):return np.array([int(code[i:i+2],16)/255 for i in (1,3,5)],np.float32)
def linear(code):
    rgb=srgb(code);return np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)

def mesh_signature(ob):
    m=ob.data;h=hashlib.sha256()
    for data,prop,width,dtype in ((m.vertices,'co',3,np.float32),(m.loops,'vertex_index',1,np.int32),
            (m.polygons,'loop_start',1,np.int32),(m.polygons,'loop_total',1,np.int32),
            (m.polygons,'material_index',1,np.int32),(m.corner_normals,'vector',3,np.float32)):
        arr=np.empty(len(data)*width,dtype=dtype);data.foreach_get(prop,arr);h.update(arr.tobytes())
    for uv in m.uv_layers:
        arr=np.empty(len(uv.data)*2,np.float32);uv.data.foreach_get('uv',arr);h.update(uv.name.encode());h.update(arr.tobytes())
    weights=[[(ob.vertex_groups[g.group].name,g.weight) for g in v.groups] for v in m.vertices]
    h.update(json.dumps(weights,separators=(',',':')).encode());h.update(np.array(ob.matrix_basis,np.float32).tobytes())
    return h.hexdigest()

def bone_signature(rig):
    return hashlib.sha256(json.dumps([(b.name,b.parent.name if b.parent else '',np.array(b.matrix_local,dtype=np.float64).ravel().tolist()) for b in rig.data.bones],separators=(',',':')).encode()).hexdigest()

def scene_settings(scene,original,resolution=2048):
    scene.world=original.world.copy();scene.render.engine=original.render.engine
    scene.render.resolution_x=scene.render.resolution_y=resolution;scene.render.resolution_percentage=100
    scene.render.film_transparent=original.render.film_transparent;scene.render.use_freestyle=False
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
    for prop in ('view_transform','look','exposure','gamma'):
        setattr(scene.view_settings,prop,getattr(original.view_settings,prop))
    if hasattr(scene,'eevee') and hasattr(scene.eevee,'taa_render_samples'):
        scene.eevee.taa_render_samples=original.eevee.taa_render_samples
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1;scene.render.fps=30
    camera=original.camera.copy();camera.data=original.camera.data.copy();scene.collection.objects.link(camera);scene.camera=camera
    scene.frame_set(1)
    return scene

def recolor_mesh(body,key,codes):
    m=body.data;attr=m.color_attributes['SSF_PaletteLinear'];values=np.empty(len(attr.data)*4,np.float32);attr.data.foreach_get('color',values);values=values.reshape(-1,4)
    original=np.array([linear(refs[key]['theme'][r]) for r in ROLES]);target=np.array([linear(c) for c in codes])
    opaque=np.ones(len(m.loops),bool)
    for p in m.polygons:
        if 'SourceTranslucent' in m.materials[p.material_index].name:opaque[list(p.loop_indices)]=False
    rows=values[opaque,:3];dist=((rows[:,None,:]-original[None,:,:])**2).sum(2);owners=dist.argmin(1)
    error=float(np.sqrt(dist.min(1)).max());assert error<.00001,(key,error)
    values[opaque,:3]=target[owners];attr.data.foreach_set('color',values.ravel());m.update()
    return {'opaque_corners':int(opaque.sum()),'source_color_max_error':error,'corner_counts_per_role':[int((owners==i).sum()) for i in range(4)]}

def atlas_variant(original,key,team,codes):
    w,h=original.size;pixels=np.empty(w*h*4,np.float32);original.pixels.foreach_get(pixels);pixels=pixels.reshape(-1,4)
    sample=pixels[::97,:3];sample=sample[(sample*sample).sum(1)>.001]
    old_codes=[refs[key]['theme'][r] for r in ROLES]
    encoded=np.array([srgb(c) for c in old_codes]);decoded=np.array([linear(c) for c in old_codes])
    def fit(palette):return float(np.min(((sample[:,None,:]-palette[None,:,:])**2).sum(2),axis=1).mean())
    is_linear=fit(decoded)<fit(encoded);old=decoded if is_linear else encoded;new=np.array([linear(c) if is_linear else srgb(c) for c in codes])
    remapped=0
    for start in range(0,len(pixels),65536):
        block=pixels[start:start+65536];distance=((block[:,:3,None]-old.T[None,:,:])**2).sum(1);owner=distance.argmin(1)
        valid=distance.min(1)<.0002;block[valid,:3]=new[owner[valid]];remapped+=int(valid.sum())
    dest=bpy.data.images.new(team+'_'+key+'_BaseColor_2K',width=w,height=h,alpha=True)
    dest.colorspace_settings.name=original.colorspace_settings.name;dest.pixels.foreach_set(pixels.ravel());dest.update()
    path=T/(team+'_'+key+'_BaseColor_2K.png');dest.filepath_raw=str(path);dest.file_format='PNG';dest.save();dest.pack()
    return dest,{'file':str(path.relative_to(O)),'resolution':[w,h],'source_pixel_space':'linear' if is_linear else 'sRGB','remapped_texels':remapped}

report={'version':'SSF_TeamPalette_B_v2','date':'2026-10-07','stage':'actual_Blender_color_candidate_pending_user_review',
    'source_blend':str(SOURCE),'source_blend_sha256':source_hash,'blender_version':bpy.app.version_string,
    'user_instruction':'蓝色方所有建筑改成图1配色，红色方把所有建筑改成图二配色，先放Blender给我审核',
    'modified_scope':KEYS,'UE_modified':False,'gameplay_integrated':False,'user_B_approval':False,
    'other_assets_recolored':False,'source_editable_parts_preserved_in_original_scenes':True,
    'ink_hex':'#1A182F','tone_factors':[.40,.72,1.],'tone_thresholds':[.38,.68],
    'role_mapping':{'Blue':{k:dict(zip(ROLES,v)) for k,v in BLUE.items()},'Red':{k:dict(zip(ROLES,v)) for k,v in RED.items()}},
    'assets':[],'texture_variants':[],'retained_original_animation_names':[a['name'] for a in construction['animations']]}
candidates={};new_materials={}
for team,palette in [('Blue',BLUE),('Red',RED)]:
    for key in KEYS:
        a=assets[key];old=bpy.data.scenes[a['scene']];codes=palette[key]
        s=scene_settings(bpy.data.scenes.new(team+'_'+key),old);bpy.context.window.scene=s
        s['team_palette_candidate']='SSF_TeamPalette_B_v2';s['team']=team;s['asset']=key
        orig_rig=bpy.data.objects[a['rig']];rig=orig_rig.copy();rig.data=orig_rig.data.copy();rig.name=team+'_'+key+'_Rig';s.collection.objects.link(rig)
        assert bone_signature(rig)==bone_signature(orig_rig)
        rig['palette_candidate_team']=team;rig['compatible_actions']=json.dumps(a['animations'])
        body0=bpy.data.objects[a['lods'][0]['body']];source_mat=body0.data.materials[0]
        portable=source_mat.node_tree.nodes['Portable_BaseColor'].image
        base,texrec=atlas_variant(portable,key,team,codes);report['texture_variants'].append(dict(texrec,team=team,asset=key))
        lod_records=[]
        for lod in a['lods']:
            col=bpy.data.collections.new(team+'_'+key+'_LOD'+str(lod['LOD']));s.collection.children.link(col)
            record={'LOD':lod['LOD'],'source_body_triangles':lod['body_triangles'],'source_outline_triangles':lod['outline_triangles'],'objects':[]}
            for role,name in [('body',lod['body']),('outline',lod['outline'])]:
                if not name:continue
                original=bpy.data.objects[name];ob=original.copy();ob.data=original.data.copy();ob.name=team+'_'+name;col.objects.link(ob)
                ob.parent=rig;ob.matrix_parent_inverse=original.matrix_parent_inverse.copy();ob.matrix_basis=original.matrix_basis.copy()
                for mod in ob.modifiers:
                    if mod.type=='ARMATURE':mod.object=rig
                prior=mesh_signature(original);assert mesh_signature(ob)==prior
                if role=='body':
                    record['palette_check']=recolor_mesh(ob,key,codes)
                    for index,mat in enumerate(list(ob.data.materials)):
                        identity=(team,key,mat.name)
                        if identity not in new_materials:
                            newmat=mat.copy();newmat.name=team+'_'+mat.name;newmat['team_palette_candidate']='SSF_TeamPalette_B_v2'
                            if 'ThreeTone' in mat.name:
                                nt=newmat.node_tree;nt.nodes['Portable_BaseColor'].image=base
                                nt.nodes['Team Color'].outputs[0].default_value=(*linear(codes[2]),1)
                                for n in nt.nodes:
                                    if n.type=='VECT_MATH' and n.operation=='DISTANCE':n.inputs[1].default_value=linear(codes[2])
                                newmat['Team Color']=codes[2];newmat.diffuse_color=(*linear(codes[0]),1)
                            else:
                                for n in newmat.node_tree.nodes:
                                    if n.type=='EMISSION' and not n.inputs['Color'].is_linked:n.inputs['Color'].default_value=(*linear(codes[2]),1)
                            new_materials[identity]=newmat
                        ob.data.materials[index]=new_materials[identity]
                assert mesh_signature(ob)==prior,(key,role,lod['LOD'],'geometry changed')
                ob['palette_candidate_team']=team;ob['palette_candidate_version']='SSF_TeamPalette_B_v2'
                ob.hide_render=lod['LOD']!=0;ob.hide_set(lod['LOD']!=0)
                record[role]=ob.name;record['objects'].append({'role':role,'name':ob.name,'source':name,'geometry_UV_normals_weights_signature':prior})
            lod_records.append(record)
        rec={'team':team,'key':key,'scene':s.name,'rig':rig.name,'bone_count':len(rig.data.bones),
             'bone_signature':bone_signature(rig),'source_compatible_animation_names':a['animations'],'lods':lod_records}
        report['assets'].append(rec);candidates[(team,key)]=rec
        print('SSF_TEAM_PALETTE_READY',team,key,flush=True)

assembly=construction['assembly'];offsets=assembly['layout_m'];oldassembly=bpy.data.scenes[assembly['scene']]
def instance(scene,rec,position,suffix):
    original_rig=bpy.data.objects[rec['rig']];rig=original_rig.copy();rig.name=suffix+'_Rig';rig.animation_data_clear();rig.location+=Vector(position);scene.collection.objects.link(rig)
    for name in (rec['lods'][0]['body'],rec['lods'][0].get('outline')):
        if not name:continue
        source=bpy.data.objects[name];ob=source.copy();ob.name=suffix+'_'+source.name;scene.collection.objects.link(ob)
        ob.parent=rig;ob.matrix_parent_inverse=source.matrix_parent_inverse.copy();ob.matrix_basis=source.matrix_basis.copy()
        for mod in ob.modifiers:
            if mod.type=='ARMATURE':mod.object=rig
        ob.hide_render=False;ob.hide_set(False)

def fit_camera(scene,direction):
    bpy.context.window.scene=scene;bpy.context.view_layer.update()
    points=[ob.matrix_world@Vector(c) for ob in scene.objects if ob.type=='MESH' and not ob.hide_render for c in ob.bound_box]
    lo=Vector([min(p[i] for p in points) for i in range(3)]);hi=Vector([max(p[i] for p in points) for i in range(3)]);center=(lo+hi)/2
    scene.camera.data.type='ORTHO';scene.camera.data.ortho_scale=max(hi-lo)*1.6
    scene.camera.location=center+Vector(direction).normalized()*max(hi-lo)*4
    scene.camera.rotation_euler=(center-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    return list(center),list(hi-lo)

for team in ('Blue','Red'):
    s=scene_settings(bpy.data.scenes.new(team+'_Buildings_Assembly'),oldassembly,4096)
    for key in KEYS:instance(s,candidates[(team,key)],offsets[key],team+'_Assembly_'+key)
    for ob in oldassembly.objects:
        if ob.type=='MESH' and ('Floor' in ob.name):
            copy=ob.copy();copy.name=team+'_Unchanged_'+ob.name;s.collection.objects.link(copy);copy.hide_render=False;copy.hide_set(False)
    center,extent=fit_camera(s,(.7,-1.2,1.4));s['review_only']=True
    report[team+'_assembly']={'scene':s.name,'center_m':center,'extent_m':extent,'platform_color_unchanged':True}

review=scene_settings(bpy.data.scenes.new('Review_Blue_Red_Buildings'),oldassembly,4096)
review.render.resolution_x=4096;review.render.resolution_y=2600
locations={k:(i%3*25.,-(i//3)*30.,0.) for i,k in enumerate(KEYS)}
for team,x in [('Blue',-90.),('Red',20.)]:
    for key in KEYS:
        offset=Vector(locations[key])+Vector((x,0,0));instance(review,candidates[(team,key)],offset,team+'_Compare_'+key)
center,extent=fit_camera(review,(.2,-1.1,1.2));review.camera.data.ortho_scale=max(extent)*1.24
report['review_scene']={'scene':review.name,'center_m':center,'extent_m':extent,'Blue_on_left_Red_on_right':True,'uses_real_project_dimensions':True}
review['review_only']=True;review['candidate_version']='SSF_TeamPalette_B_v2';review['user_B_approval']='pending'
bpy.context.window.scene=review
for window in bpy.context.window_manager.windows:
    window.scene=review
    for area in window.screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active;space.shading.type='RENDERED';space.overlay.show_overlays=False
            space.shading.use_scene_world_render=True;space.shading.use_scene_lights_render=True
            space.region_3d.view_perspective='CAMERA';space.region_3d.view_camera_zoom=0.;space.region_3d.view_camera_offset=(0.,0.)
assert len(report['assets'])==12 and len(report['retained_original_animation_names'])==22
assert all(bpy.data.actions.get(a['action']) for a in construction['animations'])
report['original_B_v1_file_preserved']=hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_hash
report['source_geometry_normals_UVs_weights_and_bones_preserved']=True
report['new_body_LOD_count']=36;report['candidate_blend']=str(OUT)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT))
report['candidate_blend_sha256']=hashlib.sha256(OUT.read_bytes()).hexdigest()
report['success']=True
(O/'candidate_manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_TEAM_PALETTE_CANDIDATE_SAVED',str(OUT),flush=True)

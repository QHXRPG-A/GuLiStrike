"""Render actual Blender geometry: matching sheet, three views and core colors."""
import bpy
import json
import math
import bmesh
from pathlib import Path
from mathutils import Vector,kdtree

OUT=Path('D:/UE5.7/test1/ArtSource/Buildings/OutpostSymmetry_20261029/Production_B_v1')
OWNER='GuLi.OutpostSymmetric.B_v1'
scene=bpy.data.scenes['OUTPOST_B_v1']
root=bpy.data.objects['OP_Root_Midplane']
parts=bpy.data.collections['OP_Editable_Parts']
previews=OUT/'Previews'


def set_color(mode):
    root['Core_Color_Mode']=mode
    root.update_tag()
    bpy.context.scene.frame_set(bpy.context.scene.frame_current)
    bpy.context.view_layer.update()


def render(path,camera,mode=0):
    bpy.context.window.scene=scene
    scene.camera=bpy.data.objects[camera]
    set_color(mode)
    scene.render.filepath=str(previews/path)
    scene.render.resolution_x=1500;scene.render.resolution_y=1700
    ground=bpy.data.objects['OP_Studio_Ground']
    previous_ground_visibility=ground.hide_render
    ground.hide_render=previous_ground_visibility or camera.endswith('Underside')
    try:bpy.ops.render.render(write_still=True)
    finally:ground.hide_render=previous_ground_visibility
    return {'path':path,'camera':camera,'core_color_mode':mode}


def geometry_audit():
    bpy.context.window.scene=scene
    bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get()
    points=[];rows=[];issues=[]
    for obj in parts.objects:
        if obj.type!='MESH':continue
        evaluated=obj.evaluated_get(dg);mesh=evaluated.to_mesh()
        mesh.calc_loop_triangles()
        bm=bmesh.new();bm.from_mesh(mesh)
        row={'name':obj.name,'triangles':len(mesh.loop_triangles),
             'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
             'degenerate_faces':sum(f.calc_area()<1e-10 for f in bm.faces),
             'loose_vertices':sum(not v.link_faces for v in bm.verts),
             'uv_layers':list(obj.data.uv_layers.keys())}
        if any(row[k] for k in ('nonmanifold_edges','degenerate_faces','loose_vertices')) or not row['uv_layers']:
            issues.append(row)
        points.extend(obj.matrix_world@v.co for v in mesh.vertices)
        rows.append(row);bm.free();evaluated.to_mesh_clear()
    tree=kdtree.KDTree(len(points))
    for i,point in enumerate(points):tree.insert(point,i)
    tree.balance()
    symmetry=max(tree.find(Vector((p.x,p.y,-p.z)))[2] for p in points)
    minimum=[min(p[i] for p in points) for i in range(3)]
    maximum=[max(p[i] for p in points) for i in range(3)]
    report={'scene':scene.name,'mesh_objects':len(rows),
            'evaluated_triangles':sum(row['triangles'] for row in rows),
            'dimensions_m':[maximum[i]-minimum[i] for i in range(3)],
            'max_horizontal_midplane_mirror_vertex_error_m':symmetry,
            'mesh_issues':issues,'all_meshes_have_uv':all(row['uv_layers'] for row in rows),
            'default_scene_preserved':list(bpy.data.scenes['Scene'].objects.keys()),'parts':rows}
    (OUT/'Evidence/geometry-audit.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    if issues or symmetry>.00001:raise RuntimeError('Geometry audit needs repair before delivery')
    return report


def make_sheet():
    board=bpy.data.scenes.get('OUTPOST_B_v1_ReviewSheet')
    if not board:
        board=bpy.data.scenes.new('OUTPOST_B_v1_ReviewSheet')
        board['asset_owner']=OWNER
    elif board.get('asset_owner')!=OWNER:raise RuntimeError('Foreign sheet scene')
    collection=bpy.data.collections.get('OP_Review_Sheet_Only')
    if collection:
        if collection.get('asset_owner')!=OWNER:raise RuntimeError('Foreign sheet collection')
        for obj in list(collection.objects):
            if obj.get('asset_owner')!=OWNER:raise RuntimeError('Foreign sheet object')
            bpy.data.objects.remove(obj,do_unlink=True)
    else:
        collection=bpy.data.collections.new('OP_Review_Sheet_Only')
        collection['asset_owner']=OWNER
    if collection.name not in board.collection.children:board.collection.children.link(collection)
    rig=bpy.data.collections['OP_Review_Rig']
    if rig.name not in board.collection.children:board.collection.children.link(rig)
    board.world=scene.world
    board.render.engine='BLENDER_EEVEE'
    board.render.resolution_x=1800;board.render.resolution_y=1200
    board.render.resolution_percentage=100
    board.render.image_settings.file_format='PNG'
    board.render.image_settings.color_mode='RGBA'
    board.view_settings.view_transform='Standard';board.view_settings.look='None'
    board.render.use_freestyle=True
    fs=board.view_layers[0].freestyle_settings
    fs.mode='EDITOR';fs.crease_angle=math.radians(130);fs.use_culling=True
    fs.use_material_boundaries=True
    while len(fs.linesets):fs.linesets.remove(fs.linesets[0])
    for source in scene.view_layers[0].freestyle_settings.linesets:
        line=fs.linesets.new(source.name)
        for key in ('select_silhouette','select_border','select_crease','select_edge_mark',
                    'select_external_contour','select_material_boundary','select_contour',
                    'select_suggestive_contour','select_ridge_valley'):
            if hasattr(line,key):setattr(line,key,getattr(source,key))
        line.linestyle=source.linestyle
    for label,position,scale,angle,pitch in (
        ('Hero',(-16.1,0,0),1,-82,-2.8),
        ('Front',(0,0,-8.2),.44,-90,0),
        ('Side',(11.1,0,-8.2),.44,0,0),
        ('Back',(22.0,0,-8.2),.44,90,0)):
        obj=bpy.data.objects.new('OP_Sheet_'+label,None)
        collection.objects.link(obj);obj['asset_owner']=OWNER
        obj.instance_type='COLLECTION';obj.instance_collection=parts
        obj.location=position;obj.scale=(scale,scale,scale)
        obj.rotation_euler=(math.radians(pitch),0,math.radians(angle))
    ink=bpy.data.materials.get('OP_Sheet_Ink') or bpy.data.materials.new('OP_Sheet_Ink')
    ink['asset_owner']=OWNER;ink.use_nodes=True
    nodes,links=ink.node_tree.nodes,ink.node_tree.links;nodes.clear()
    emission=nodes.new('ShaderNodeEmission');emission.inputs['Color'].default_value=(.012,.010,.029,1)
    output=nodes.new('ShaderNodeOutputMaterial');links.new(emission.outputs[0],output.inputs['Surface'])
    for i,(text,x,z,size) in enumerate((
        ('OUTPOST / B_v1',-26.2,16.3,.82),
        ('FRONT +X',-2.7,-16.4,.50),('SIDE -Y',8.5,-16.4,.50),
        ('BACK -X',19.2,-16.4,.50))):
        data=bpy.data.curves.new('OP_Sheet_Text_%02d'%i,'FONT')
        data.body=text;data.size=size
        obj=bpy.data.objects.new(data.name,data)
        obj['asset_owner']=OWNER;collection.objects.link(obj)
        obj.location=(x,-20,z);obj.rotation_euler=(math.pi/2,0,0)
        data.materials.append(ink)
    data=bpy.data.cameras.new('OP_Sheet_Camera_Data')
    camera=bpy.data.objects.new('OP_Sheet_Camera',data)
    camera['asset_owner']=OWNER;collection.objects.link(camera)
    camera.location=(0,-150,0);camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
    data.type='ORTHO';data.ortho_scale=57
    board.camera=camera
    bpy.context.window.scene=board
    set_color(0)
    board.render.filepath=str(previews/'08_Review_Sheet.png')
    bpy.ops.render.render(write_still=True)
    return board


def finish():
    audit=geometry_audit()
    shots=[]
    for path,camera,mode in (
        ('01_Hero.png','OP_Camera_Hero',0),
        ('02_Front.png','OP_Camera_Front',0),
        ('03_Side.png','OP_Camera_Side',0),
        ('04_Back.png','OP_Camera_Back',0),
        ('05_Underside.png','OP_Camera_Underside',0),
        ('06_Core_Blue.png','OP_Camera_Hero',1),
        ('07_Core_Red.png','OP_Camera_Hero',2)):
        shots.append(render(path,camera,mode))
    make_sheet()
    bpy.context.window.scene=scene
    scene.camera=bpy.data.objects['OP_Camera_Hero']
    set_color(0)
    # Source text and native properties survive reopening; panel is a live convenience.
    controls=OUT/'core_light_controls.py'
    for name,path in (('OP_Core_Light_Controls.py',controls),
                      ('OUTPOST_B_v1_Build.py',OUT/'build_outpost.py'),
                      ('OUTPOST_B_v1_Review.py',Path(__file__))):
        text=bpy.data.texts.get(name) or bpy.data.texts.new(name)
        text.clear();text.write(path.read_text(encoding='utf8'))
        text.use_module=False
        text.use_fake_user=True
    exec(compile(controls.read_text(encoding='utf8'),str(controls),'exec'),{'__name__':'__outpost_controls__'})
    for obj in bpy.context.selected_objects:obj.select_set(False)
    root.select_set(True);bpy.context.view_layer.objects.active=root
    for area in bpy.context.screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.show_region_ui=True
            area.spaces.active.shading.type='MATERIAL'
            area.spaces.active.shading.use_scene_lights=True
            area.spaces.active.shading.use_scene_world=True
            for region in area.regions:
                if region.type=='UI':
                    try:region.active_panel_category='GuLi'
                    except Exception:pass
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Outpost_Symmetric_B_v1.blend'))
    record={'actual_blender_renders':shots,'sheet':'08_Review_Sheet.png',
            'model_triangles':audit['evaluated_triangles'],'default_core_mode':0,
            'core_mode_values':{'0':'Neutral white','1':'Ally blue #6AA4BE','2':'Enemy red #A34053'},
            'source_blend':str(OUT/'Outpost_Symmetric_B_v1.blend'),'user_review_B':'pending','ue_imported':False}
    (OUT/'Evidence/preview-delivery.json').write_text(json.dumps(record,indent=2),encoding='utf8')
    return record


DELIVERY_REPORT=finish()

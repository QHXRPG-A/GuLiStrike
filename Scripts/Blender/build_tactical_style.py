"""Deterministic rigid-part remesh and atlas authoring for the approved vehicles."""
import bpy,bmesh,json,math,sys
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
import numpy as np

ROOT=Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916')
SRC=json.loads((ROOT/'source_manifest.json').read_text(encoding='utf8'))
REFLECT=Matrix.Diagonal((1,-1,1,1))
PALETTE=['C84B29','EF7340','EEE9D8','293D43','667979','F9C369']
def lin(hex):
    rgb=[int(hex[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)+(1,)
def single(o):
    if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
def matrix(t):
    x,y,z,w=t['rotation']
    m=Quaternion((w,x,y,z)).to_matrix().to_4x4()@Matrix.Diagonal(tuple(t['scale'])+(1,))
    m.translation=Vector(t['translation'])*.01
    return REFLECT@m@REFLECT
def bounds(objects):
    pts=[o.matrix_world@v.co for o in objects for v in o.data.vertices]
    return Vector([min(p[i] for p in pts) for i in range(3)]),Vector([max(p[i] for p in pts) for i in range(3)])
def tris(o):return sum(len(p.vertices)-2 for p in o.data.polygons)
def material(i):
    name='TS_'+str(i);m=bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color=lin(PALETTE[i]);m.use_nodes=True
    b=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED');b.inputs['Base Color'].default_value=m.diffuse_color;b.inputs['Roughness'].default_value=.9
    return m
def paint(o,kind):
    o.data.materials.clear()
    for i in range(len(PALETTE)):o.data.materials.append(material(i))
    for p in o.data.polygons:
        nz=p.normal.z
        p.material_index=3 if kind.startswith('Wheel') else (2 if nz>.72 else 0 if nz>-.6 else 3)
        if 'Fan' in o.name:p.material_index=2 if nz>.8 else 0 if nz>.25 else 3
        if 'Cannon' in o.name:p.material_index=3 if nz<.2 else 0
        if 'Antenna' in o.name:p.material_index=3
    o['motion']=kind
def import_unit(unit):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts=[]
    for c in SRC['units'][unit]['components']:
        if unit=='Sweeper' and 'Cockpit' in c['name']:continue
        before=set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=SRC['meshes'][c['mesh']]['file'],use_anim=False)
        imported=[o for o in bpy.data.objects if o not in before]
        meshes=[o for o in imported if o.type=='MESH']
        helpers=[o for o in imported if o.type!='MESH']
        for o in meshes:
            bpy.context.view_layer.update()
            o.data.transform(o.matrix_world);o.parent=None;o.matrix_world=Matrix.Identity(4)
            for mod in list(o.modifiers):o.modifiers.remove(mod)
            o.data.transform(matrix(c['transform']));o.data.update()
            if unit=='Sweeper' and 'bones' in c:
                groups={g.index:g.name for g in o.vertex_groups}
                by_group={}
                for p in o.data.polygons:
                    weights={}
                    for vi in p.vertices:
                        for g in o.data.vertices[vi].groups:
                            n=groups[g.group];weights[n]=weights.get(n,0)+g.weight
                    group=max(weights,key=weights.get) if weights else 'Body'
                    if not group.startswith('Wheel'):group='Body'
                    by_group.setdefault(group,[]).append(p)
                for group,polys in by_group.items():
                    ids=sorted(set(v for p in polys for v in p.vertices));remap={v:i for i,v in enumerate(ids)}
                    m=bpy.data.meshes.new(unit+'_'+group);m.from_pydata([o.data.vertices[i].co for i in ids],[],[[remap[v] for v in p.vertices] for p in polys]);m.update()
                    n=bpy.data.objects.new(unit+'_'+group,m);bpy.context.collection.objects.link(n);paint(n,group);parts.append(n)
                bpy.data.objects.remove(o,do_unlink=True)
            else:
                o.name=unit+'_'+c['name'];paint(o,'Body');parts.append(o)
        for o in helpers:bpy.data.objects.remove(o,do_unlink=True)
    return parts
def preview(parts,unit,suffix):
    lo,hi=bounds(parts);center=(lo+hi)*.5;span=max(hi-lo)
    scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1000;scene.render.resolution_y=850;scene.render.resolution_percentage=100
    scene.world=bpy.data.worlds.new(unit+'World');scene.world.color=(.12,.12,.12)
    scene.view_settings.view_transform='Standard'
    camdata=bpy.data.cameras.new('PreviewCamera');cam=bpy.data.objects.new('PreviewCamera',camdata);scene.collection.objects.link(cam)
    cam.location=center+Vector((1.0,-1.35,.95))*span;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();camdata.type='ORTHO';camdata.ortho_scale=span*1.38;camdata.clip_end=10000;scene.camera=cam
    for name,loc,energy in [('Key',(1,-2,4),3),('Fill',(-3,1,1),1)]:
        data=bpy.data.lights.new(name,'SUN');light=bpy.data.objects.new(name,data);scene.collection.objects.link(light);light.rotation_euler=Vector(loc).to_track_quat('Z','Y').to_euler();data.energy=energy;data.angle=.15
    scene.render.image_settings.file_format='PNG';scene.render.filepath=str(ROOT/(unit+'_'+suffix+'.png'));bpy.ops.render.render(write_still=True)
    return {'min':list(lo),'max':list(hi),'triangles':sum(tris(o) for o in parts),'parts':{o.name:{'triangles':tris(o),'motion':o['motion'],'bounds':[list(v) for v in bounds([o])]} for o in parts}}

if __name__=='__main__':
    for unit in ('Sweeper','WarMachine'):
        parts=import_unit(unit)
        report=preview(parts,unit,'source')
        (ROOT/(unit+'_assembly.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
        bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/(unit+'_assembly.blend')))
        print('ASSEMBLY',unit,report['triangles'],report['min'],report['max'],flush=True)

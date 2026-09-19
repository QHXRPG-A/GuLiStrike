"""Whole component color replacements; no stripes or spatial paint cuts."""
import bpy,json,math,hashlib,collections
import numpy as np
from pathlib import Path
from mathutils import Vector
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919');OUT=ROOT/'Production_v5_Parts';OUT.mkdir(exist_ok=True);(OUT/'Previews').mkdir(exist_ok=True)
baseline=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text())
report={'success':False,'source':bpy.data.filepath,'method':'Whole original connected surface components, never coordinate stripes','parts':{},'palette_srgb':{'red':'A6534C','blue':'587F9B','white':'E3E6E0'}}
def linear(h):
    c=[int(h[i:i+2],16)/255 for i in (0,2,4)];return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c)+(1,)
def islands(o):
    me=o.data;parent=list(range(len(me.vertices)))
    def root(a):
        while parent[a]!=a:parent[a]=parent[parent[a]];a=parent[a]
        return a
    edges=np.empty(len(me.edges)*2,np.int32);me.edges.foreach_get('vertices',edges)
    for a,b in edges.reshape(-1,2):
        a,b=root(int(a)),root(int(b))
        if a!=b:parent[b]=a
    roots=np.array([root(i) for i in range(len(me.vertices))],np.int32)
    co=np.empty(len(me.vertices)*3,np.float32);me.vertices.foreach_get('co',co);co=co.reshape(-1,3)
    loops=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',loops)
    starts=np.empty(len(me.polygons),np.int32);me.polygons.foreach_get('loop_start',starts)
    counts=np.empty(len(me.polygons),np.int32);me.polygons.foreach_get('loop_total',counts)
    colors=np.empty(len(me.loops)*4,np.float32);me.color_attributes['SourceRegion_CleanPaint'].data.foreach_get('color',colors);colors=colors.reshape(-1,4)
    return roots,co,loops,starts,counts,colors
def repaint(o,color,select):
    me=o.data;roots,co,loops,starts,counts,colors=islands(o);froots=roots[loops[starts]];chosen=[]
    areas=np.empty(len(me.polygons),np.float32);me.polygons.foreach_get('area',areas)
    totals=np.bincount(froots,weights=areas,minlength=len(me.vertices))
    blue=np.bincount(froots,weights=areas*(colors[starts,2]>colors[starts,0]*1.12),minlength=len(me.vertices))
    for ri in np.flatnonzero(totals>0):
        ids=np.flatnonzero(roots==ri);pts=co[ids];lo=pts.min(0);hi=pts.max(0)
        if not select(int(ri),lo,hi,float(totals[ri]),float(blue[ri]/totals[ri])):continue
        faces=np.flatnonzero(froots==ri)
        for fi in faces:colors[starts[fi]:starts[fi]+counts[fi]]=linear(color)
        chosen.append({'root':int(ri),'vertices':len(ids),'faces':len(faces),'min':lo.tolist(),'max':hi.tolist(),'color':color})
    me.color_attributes['SourceRegion_CleanPaint'].data.foreach_set('color',colors.reshape(-1));me.update();report['parts'][o.name]=chosen

# Each outer leg's upper cover is several original FBX surface islands. Select
# those complete panels, including their side returns, by the whole-part bounds.
sp=bpy.data.objects['WORK_SpiderMech__SpiderMech.002']
def spider_cover(ri,lo,hi,area,blue):
    side_min=min(abs(lo[0]),abs(hi[0]))
    return blue>.8 and side_min>89 and lo[2]>199 and hi[2]>230 and hi[0]-lo[0]>40 and area>500
repaint(sp,report['palette_srgb']['red'],spider_cover)

# Four connected source pieces form the two complete upper-leg armor housings.
leg=bpy.data.objects['WORK_Mech_Lightest__Mech_Legs_Lt.001']
repaint(leg,report['palette_srgb']['white'],lambda ri,lo,hi,area,blue:ri in (404,222,214,298))
fairing=bpy.data.objects['WORK_Mech_Lightest__Sealed_Armor_Fairing']
mat=fairing.data.materials[0].copy();mat.name='PART5_LightMech_ClosedArmor_Blue';fairing.data.materials[0]=mat
mat.node_tree.nodes['Paint_x_Three_Tones'].inputs[1].default_value=linear(report['palette_srgb']['blue'])
next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED').inputs['Base Color'].default_value=linear(report['palette_srgb']['blue'])
report['parts'][fairing.name]=[{'entire_object':True,'color':report['palette_srgb']['blue']}]

for name,parts in report['parts'].items():
    if name==fairing.name:continue
    positive=[p for p in parts if p['min'][0]>0];negative=[p for p in parts if p['max'][0]<0]
    assert len(positive)==len(negative),(name,len(positive),len(negative))
    errors=[]
    for p in positive:
        mirrored=np.array([-p['max'][0],p['min'][1],p['min'][2],-p['min'][0],p['max'][1],p['max'][2]])
        errors.append(min(float(np.max(np.abs(mirrored-np.array(q['min']+q['max'])))) for q in negative))
    assert max(errors)<.02,(name,errors)
    report.setdefault('mirrored_part_pairs',{})[name]={'pairs':len(positive),'max_bounds_difference_cm':max(errors)}

def render(key,view):
    scene=bpy.data.scenes['Review_'+key];bpy.context.window.scene=scene;bpy.context.view_layer.update();cam=scene.camera
    bounds=baseline['assets'][key]['bounds'];lo=Vector(bounds['min']);hi=Vector(bounds['max']);center=(lo+hi)/2;span=max(hi-lo)
    d={'Hero':Vector((1.5,-2,1.05)),'Front':Vector((0,-1,0)),'Side':Vector((1,0,0)),'Rear':Vector((0,1,0))}[view].normalized()
    cam.location=center+d*span*3;rot=(center-cam.location).to_track_quat('-Z','Y');cam.rotation_euler=rot.to_euler()
    pts=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)];right=rot@Vector((1,0,0));up=rot@Vector((0,1,0))
    w=max(p.dot(right) for p in pts)-min(p.dot(right) for p in pts);h=max(p.dot(up) for p in pts)-min(p.dot(up) for p in pts);cam.data.ortho_scale=max(w,h*scene.render.resolution_x/scene.render.resolution_y)*1.12
    scene.render.filepath=str(OUT/'Previews'/(key+'_'+view+'.png'));bpy.ops.render.render(write_still=True)
for key in ('SpiderMech','Mech_Lightest'):
    for view in ('Hero','Front','Side','Rear'):render(key,view)
    changed=[]
    for o in bpy.data.collections['WORK_'+key].objects:
        if o.type!='MESH' or o.get('ink_layer'):continue
        for mat in o.data.materials:
            for e in mat.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp.elements:changed.append((e,e.color[:]));e.color=(1,1,1,1)
    bpy.context.scene.render.filepath=str(OUT/'Previews'/(key+'_Rear_Paint.png'));bpy.ops.render.render(write_still=True)
    for e,c in changed:e.color=c
bpy.context.window.scene=bpy.data.scenes['Review_Mech_Lightest']
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_ComponentColors_v5.blend'),check_existing=False)
report['success']=True
(OUT/'component_paint_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('WHOLE_COMPONENT_COLOR_READY',flush=True)

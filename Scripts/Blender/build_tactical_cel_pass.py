"""Bake structural ink and export two-band shadows plus inverted-hull contours.

Uses the approved authored meshes, not generated geometry. It writes a new
production variant and never overwrites the editable modeling source.
"""
import bpy,bmesh,sys,math,json
from pathlib import Path
from mathutils import Vector
import numpy as np

SCRIPTS=Path(__file__).parent;sys.path.insert(0,str(SCRIPTS))
import build_tactical_from_drawings as author
import package_tactical_handbuilt as package
SOURCE=author.OUT
OUT=Path('D:/UE5.7/test1/ArtSource/StylePass_20260917/Models');OUT.mkdir(parents=True,exist_ok=True)
INK=author.linear('172D40')

def ink_mask(o,unit,scale):
    me=o.data;uv=me.uv_layers['Atlas'];me.calc_loop_triangles()
    color=next(n.image for n in o.data.materials[0].node_tree.nodes if n.type=='TEX_IMAGE')
    raw=bpy.data.images.load(str(SOURCE/('T_'+unit+'_BaseColor.png')),check_existing=False);raw.colorspace_settings.name='Non-Color'
    w,h=raw.size;pixels=np.array(raw.pixels[:],dtype=np.float32).reshape(h,w,4)
    def face_color(p):
        xy=np.array([uv.data[i].uv[:] for i in p.loop_indices]).mean(0)
        return pixels[min(h-1,max(0,int(xy[1]*h))),min(w-1,max(0,int(xy[0]*w))),:3]
    groups={g.index:g.name for g in o.vertex_groups}
    keys=[(groups[v.groups[0].group],*(round(c/scale,5) for c in v.co)) for v in me.vertices]
    edges={};colors=[face_color(p) for p in me.polygons]
    for p in me.polygons:
        ids=list(p.vertices)
        for ia,ib in zip(ids,ids[1:]+ids[:1]):
            key=tuple(sorted((keys[ia],keys[ib])))
            edges.setdefault(key,[]).append((p.index,ia,ib))
    selected={};count=0;radius=.010*scale
    for entries in edges.values():
        pa,ia,ib=entries[0];start=np.array(me.vertices[ia].co);end=np.array(me.vertices[ib].co)
        if np.linalg.norm(end-start)<.11*scale:continue
        draw=len(entries)==1
        if len(entries)==2:
            pb=entries[1][0]
            angle=me.polygons[pa].normal.angle(me.polygons[pb].normal)
            draw=angle>math.radians(38) or np.linalg.norm(colors[pa]-colors[pb])>.27
        if not draw:continue
        count+=1
        for pi,_,_ in entries:selected.setdefault(pi,[]).append((start,end))
    mask=np.zeros((h,w),np.float32);filled=np.zeros((h,w),bool)
    for t in me.loop_triangles:
        xy=np.array([uv.data[i].uv[:] for i in t.loops])*[w,h]
        a,b=xy[1]-xy[0],xy[2]-xy[0];det=a[0]*b[1]-a[1]*b[0]
        if abs(det)<1e-8:continue
        x0,y0=np.maximum(np.floor(xy.min(0)).astype(int),0);x1,y1=np.minimum(np.ceil(xy.max(0)).astype(int),[w,h])
        if x0>=x1 or y0>=y1:continue
        dx=np.arange(x0,x1)[None,:]+.5-xy[0,0];dy=np.arange(y0,y1)[:,None]+.5-xy[0,1]
        s=(dx*b[1]-dy*b[0])/det;u=(a[0]*dy-a[1]*dx)/det;inside=(s>=0)&(u>=0)&(s+u<=1)
        if not inside.any():continue
        filled[y0:y1,x0:x1][inside]=True
        if t.polygon_index not in selected:continue
        vv=np.array([me.vertices[i].co[:] for i in t.vertices]);points=vv[0]+s[inside,None]*(vv[1]-vv[0])+u[inside,None]*(vv[2]-vv[0])
        aa=max(radius*.33,math.sqrt(np.linalg.norm(np.cross(vv[1]-vv[0],vv[2]-vv[0]))/max(abs(det),1e-8))*.65)
        strength=np.zeros(len(points),np.float32)
        for start,end in selected[t.polygon_index]:
            vec=end-start;along=np.clip(((points-start)*vec).sum(1)/np.dot(vec,vec),0,1)
            distance=np.linalg.norm(points-start-along[:,None]*vec,axis=1)
            strength=np.maximum(strength,np.clip((radius+aa-distance)/(2*aa),0,1))
        mask[y0:y1,x0:x1][inside]=strength
    # Propagate only covered values into the existing atlas gutter.
    for _ in range(6):
        total=np.zeros_like(mask);n=np.zeros_like(mask)
        for ax,shift in ((0,1),(0,-1),(1,1),(1,-1)):
            f=np.roll(filled,shift,ax);total+=np.roll(mask,shift,ax)*f;n+=f
        grow=(~filled)&(n>0);mask[grow]=total[grow]/n[grow];filled|=grow
    image=bpy.data.images.new('T_'+unit+'_LineMask',width=w,height=h,alpha=False);image.colorspace_settings.name='Non-Color'
    rgba=np.ones((h,w,4),np.float32);rgba[...,:3]=mask[...,None];image.pixels.foreach_set(rgba.ravel())
    image.filepath_raw=str(OUT/('T_'+unit+'_LineMask.png'));image.file_format='PNG';image.save();image.pack()
    return color,image,{'structural_edges':count,'line_radius_m':radius,'mask_size':[w,h],'ink_fraction':float((mask>.5).mean())}

def cel_material(unit,atlas,mask):
    m=bpy.data.materials.new('M_'+unit+'_Cel');m.use_nodes=True
    n,l=m.node_tree.nodes,m.node_tree.links;n.clear()
    out=n.new('ShaderNodeOutputMaterial');em=n.new('ShaderNodeEmission');l.new(em.outputs[0],out.inputs[0])
    tex=n.new('ShaderNodeTexImage');tex.image=atlas;tex.interpolation='Linear'
    ink=n.new('ShaderNodeTexImage');ink.image=mask;ink.interpolation='Linear'
    geo=n.new('ShaderNodeNewGeometry');dot=n.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized();l.new(geo.outputs['Normal'],dot.inputs[0])
    ramp=n.new('ShaderNodeValToRGB');ramp.name='Three clear cel tones';ramp.color_ramp.interpolation='CONSTANT';ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
    for i,(p,c) in enumerate([(0,(.43,.52,.66,1)),(.12,(.72,.78,.88,1)),(.55,(1,1,1,1))]):
        e=ramp.color_ramp.elements[0] if i==0 else ramp.color_ramp.elements.new(p);e.position=p;e.color=c
    l.new(dot.outputs['Value'],ramp.inputs[0]);mul=n.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1
    l.new(tex.outputs['Color'],mul.inputs[1]);l.new(ramp.outputs['Color'],mul.inputs[2])
    mix=n.new('ShaderNodeMixRGB');mix.inputs[2].default_value=INK;l.new(ink.outputs['Color'],mix.inputs[0]);l.new(mul.outputs[0],mix.inputs[1]);l.new(mix.outputs[0],em.inputs['Color'])
    m.diffuse_color=(.65,.22,.09,1)
    return m

def outline(o,unit,scale):
    shell=o.copy();shell.data=o.data.copy();shell.name='SM_'+unit+'_Contour';bpy.context.collection.objects.link(shell)
    for mod in list(shell.modifiers):
        if mod.type!='ARMATURE':shell.modifiers.remove(mod)
    me=shell.data
    # Merge positional normals by rigid part so UV boundaries cannot crack the
    # outer hull, but moving wheels and gun remain independent of the chassis.
    keys=[(v.groups[0].group,*(round(c/scale,5) for c in v.co)) for v in me.vertices]
    normal={}
    for p in me.polygons:
        ids=list(p.vertices)
        for j,vi in enumerate(ids):
            a=(me.vertices[ids[j-1]].co-me.vertices[vi].co).normalized();b=(me.vertices[ids[(j+1)%len(ids)]].co-me.vertices[vi].co).normalized()
            normal.setdefault(keys[vi],Vector());normal[keys[vi]]+=p.normal*a.angle(b,0)
    width=.026*scale
    for v in me.vertices:v.co+=normal[keys[v.index]].normalized()*width
    bm=bmesh.new();bm.from_mesh(me);bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free();me.update()
    m=bpy.data.materials.new('M_Tactical_Contour');m.use_nodes=True;m.use_backface_culling=True
    n,l=m.node_tree.nodes,m.node_tree.links;n.clear();out=n.new('ShaderNodeOutputMaterial');em=n.new('ShaderNodeEmission');em.inputs['Color'].default_value=INK;l.new(em.outputs[0],out.inputs[0]);m.diffuse_color=INK
    me.materials.clear();me.materials.append(m)
    for p in me.polygons:p.material_index=0
    # Geometric expansion and reversed winding are exported explicitly.
    shell['outline_width_m']=width;shell['outline_type']='Inverted hull; no cast shadow in UE'
    return shell

def main(unit):
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE/(unit+'_Handbuilt_Rigged.blend')))
    report=json.loads((SOURCE/(unit+'_delivery.json')).read_text(encoding='utf8'))
    body=next(o for o in bpy.context.scene.objects if o.type=='MESH');arm=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    atlas,mask,details=ink_mask(body,unit,report['source_scale']);body.data.materials[0]=cel_material(unit,atlas,mask)
    shell=outline(body,unit,report['source_scale'])
    author.OUT=OUT;author.PARTS=[body,shell]
    for view in ('three_quarter','front','side'):author.render(unit+'_Cel',view)
    for o in (body,shell):o.hide_set(False)
    author.active(body);shell.select_set(True)
    fbx=dict(use_selection=True,global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',add_leaf_bones=False,bake_anim=False,path_mode='COPY',use_custom_props=True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/('SM_'+unit+'_Cel.fbx')),object_types={'MESH'},**fbx)
    arm.select_set(True);bpy.ops.export_scene.fbx(filepath=str(OUT/('SK_'+unit+'_Cel.fbx')),object_types={'MESH','ARMATURE'},use_armature_deform_only=True,**fbx)
    report.update(cel=details,contour_width_m=shell['outline_width_m'],body_triangles=sum(len(p.vertices)-2 for p in body.data.polygons),contour_triangles=sum(len(p.vertices)-2 for p in shell.data.polygons),material_slots=2)
    report['body_bounds_m']=report['bounds_m']
    lo,hi,_,err=author.evaluated_stats([body,shell]);report['bounds_m']=[list(lo),list(hi)];report['symmetry_error_m']=err
    report['triangles']=report['body_triangles']+report['contour_triangles'];report['runtime_light']='Three bands from world normal and an art-directed directional-light parameter; no engine shading-model change'
    author.render(unit+'_Cel','three_quarter')
    bpy.context.scene.name=unit+'_Cel_Review'
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(unit+'_Cel.blend')))
    (OUT/(unit+'_cel_report.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('CEL_MODEL_READY',unit,json.dumps(details),flush=True)

if __name__=='__main__':
    unit=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    for u in ('Sweeper','WarMachine') if unit=='all' else (unit,):main(u)

import bpy,json,collections,sys
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parent
SOURCE=Path(bpy.data.filepath)

def tree():
    scene=next(s for s in bpy.data.scenes if s.name.startswith('00'))
    bpy.context.window.scene=scene;bpy.context.view_layer.update()
    coords=[];faces=[];names=[]
    for o in scene.objects:
        if o.type!='MESH' or o.get('is_lineart') or 'Studio' in o.name:continue
        base=len(coords)
        coords.extend(o.matrix_world@v.co for v in o.data.vertices)
        faces.extend(tuple(base+i for i in p.vertices) for p in o.data.polygons)
        names.extend([o.name]*len(o.data.polygons))
    return BVHTree.FromPolygons(coords,faces),names

high,names=tree()
samples=[]
for view in ('top','left','right'):
    for a in range(61):
        for b in range(81):
            if view=='top':point=Vector((-1.2+a*.04,-1.9+b*.05,4.));direction=Vector((0,0,-1))
            else:point=Vector(((-4 if view=='left' else 4),-1.9+b*.05,1.6+a*.02));direction=Vector(((1 if view=='left' else -1),0,0))
            hit,normal,index,distance=high.ray_cast(point,direction)
            if hit is not None:
                samples.append((view,point,direction,names[index],hit))
filename=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'quadruped-mech-vertex-animation-10k.blend'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE.with_name(filename)),load_ui=False)
low,low_names=tree()
changes=[];totals=collections.Counter();changed=collections.Counter()
for view,p,d,name,h in samples:
    if '蓝灰' not in name and not name.startswith('Side_Panel'):continue
    totals[(view,name)]+=1
    point,normal,idx,dist=low.ray_cast(p,d)
    new=low_names[idx] if point is not None else None
    if new!=name:
        changed[(view,name,new)]+=1
        changes.append({'view':view,'source':name,'new':new,'world':list(h)})
record={'totals':[(list(k),v) for k,v in totals.items()],'changes':[(list(k),v) for k,v in changed.most_common()],'samples':changes}
(ROOT/('coverage-v2.json' if 'v2' in filename else 'coverage-diagnosis.json')).write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
for k,v in changed.most_common(25):print(k,v,flush=True)
print('COVERAGE_COMPLETE',sum(totals.values()),len(changes),flush=True)

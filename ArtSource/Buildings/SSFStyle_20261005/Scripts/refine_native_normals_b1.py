"""Transport approved corner normals using face-interior queries, avoiding corner ambiguity."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(O/'SSF_Production_B_v1.blend'))
report=json.loads((O/'construction_report.json').read_text(encoding='utf8'))
refs={a['key']:a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets']}
stats=[]
for a in report['assets']:
 key=a['key'];scene=bpy.data.scenes[a['scene']];bpy.context.window.scene=scene
 source={}
 for index,part in enumerate(a['parts']):
  helper=bpy.data.objects[part['source_helper']];m=helper.data;m.calc_loop_triangles();world=helper.matrix_world.copy()
  points=[world@v.co for v in m.vertices];triangles=list(m.loop_triangles)
  tree=BVHTree.FromPolygons(points,[list(t.vertices) for t in triangles],all_triangles=True)
  normalworld=world.to_3x3().inverted().transposed()
  normals=[(normalworld@n.vector).normalized() for n in m.corner_normals]
  source[index]=(points,triangles,tree,normals)
 targets=[(e['LOD'],bpy.data.objects[e['body']]) for e in a['lods']]
 targets.extend((e['LOD'],bpy.data.objects[e['outline']]) for e in a['lods'] if e['outline'])
 for lod,body in targets:
  m=body.data;component=m.attributes['SSF_ComponentID'];values=[Vector((0,0,1)) for _ in m.loops];errors=[]
  for p in m.polygons:
   points,tris,tree,normal=source[component.data[p.index].value]
   for li in p.loop_indices:
    point=m.vertices[m.loops[li].vertex_index].co
    # A point exactly on a crease can return either adjoining face. Query just
    # inside this face, then interpolate at the actual vertex on that face.
    query=point*.995+p.center*.005;near,norm,idx,distance=tree.find_nearest(query);tri=tris[idx]
    v0,v1,v2=[points[i] for i in tri.vertices];u=v1-v0;v=v2-v0;w=point-v0
    aa,bb,cc,dd,ee=u.dot(u),u.dot(v),v.dot(v),w.dot(u),w.dot(v);den=aa*cc-bb*bb
    if abs(den)<1e-14:n=norm
    else:
     f=(cc*dd-bb*ee)/den;g=(aa*ee-bb*dd)/den
     n=normal[tri.loops[0]]*(1-f-g)+normal[tri.loops[1]]*f+normal[tri.loops[2]]*g
    if n.length_squared<1e-10:n=p.normal.copy()
    values[li]=n.normalized();errors.append(math.degrees(m.corner_normals[li].vector.angle(values[li],0)))
   p.use_smooth=True
  m.normals_split_custom_set([tuple(n) for n in values]);m.update()
  stats.append({'asset':key,'object':body.name,'LOD':lod,'average_corner_normal_correction_degrees':sum(errors)/max(len(errors),1),'maximum_degrees':max(errors) if errors else 0})
 if key in ('AirBase','CloningCenter','MilitaryFactory'):
  cam=refs[key]['views']['Hero'];scene.camera.location=cam['camera_location_m'];scene.camera.rotation_euler=cam['rotation_rad'];scene.camera.data.ortho_scale=cam['ortho_scale_m'];scene.render.resolution_percentage=50
  scene.render.filepath=str(O/(key+'_Refined_Hero.png'));bpy.ops.render.render(write_still=True);scene.render.resolution_percentage=100
(O/'normal_refinement_report.json').write_text(json.dumps(stats,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(O/'SSF_Production_B_v1.blend'))
print('SSF_FACE_AWARE_NORMALS_OK',stats[:3],flush=True)

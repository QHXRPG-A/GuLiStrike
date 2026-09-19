import bpy,json,collections
import numpy as np
from pathlib import Path
o=bpy.data.objects['WORK_SpiderMech__SpiderMech.002'];me=o.data
parent=list(range(len(me.vertices)))
def find(a):
    while parent[a]!=a:parent[a]=parent[parent[a]];a=parent[a]
    return a
edges=np.empty(len(me.edges)*2,np.int32);me.edges.foreach_get('vertices',edges)
for a,b in edges.reshape(-1,2):
    a,b=find(int(a)),find(int(b))
    if a!=b:parent[b]=a
roots=np.array([find(i) for i in range(len(me.vertices))],np.int32)
co=np.empty(len(me.vertices)*3,np.float32);me.vertices.foreach_get('co',co);co=co.reshape(-1,3)
loops=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',loops)
starts=np.empty(len(me.polygons),np.int32);me.polygons.foreach_get('loop_start',starts)
froots=roots[loops[starts]]
areas=np.empty(len(me.polygons),np.float32);me.polygons.foreach_get('area',areas)
colors=np.empty(len(me.loops)*4,np.float32);me.color_attributes['SourceRegion_CleanPaint'].data.foreach_get('color',colors);colors=colors.reshape(-1,4)[starts]
area_sum=np.bincount(froots,weights=areas,minlength=len(me.vertices))
blue=np.bincount(froots,weights=areas*(colors[:,2]>colors[:,0]*1.12),minlength=len(me.vertices))
rows=[]
for root in np.flatnonzero((area_sum>20)&(blue>area_sum*.8)):
    ids=np.flatnonzero(roots==root);points=co[ids];lo=points.min(0);hi=points.max(0)
    if hi[0]<80 or hi[2]<210:continue
    groups=collections.Counter(max(me.vertices[int(i)].groups,key=lambda g:g.weight).group for i in ids if me.vertices[int(i)].groups)
    rows.append({'root':int(root),'verts':len(ids),'faces':int((froots==root).sum()),'area':round(area_sum[root],1),'min':[round(float(v),2) for v in lo],'max':[round(float(v),2) for v in hi],'group':o.vertex_groups[groups.most_common(1)[0][0]].name})
rows.sort(key=lambda r:-r['area'])
dest=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/Production_v4_Accents/spider_part_diagnosis.json');dest.write_text(json.dumps(rows,indent=2))
result={'candidates':rows[:35],'file':str(dest)}

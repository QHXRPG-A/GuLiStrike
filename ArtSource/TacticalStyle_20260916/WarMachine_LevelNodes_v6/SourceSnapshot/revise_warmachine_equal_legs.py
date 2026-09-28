"""v4 Blender model review: front supports match the retained rear pair.

Only two front beams change shape; front ankle/disc assemblies translate as
rigid units. Each disc stays horizontal; the front pair is intentionally higher.
"""
import bpy
import bmesh
import math
import json
import sys
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).parent))
import revise_warmachine_slope30 as slope

review, design, author = slope.review, slope.design, slope.author
review.SOURCE = review.ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_Slope30_v3'
review.SOURCE_EDITABLE = review.SOURCE/'WarMachine_Slope30_Editable.blend'
review.OUT = review.ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_EqualLegs_v4'
review.PREVIEWS = review.OUT/'Previews'
review.EDITABLE = review.OUT/'WarMachine_EqualLegs_Editable.blend'
review.REVIEW = review.OUT/'WarMachine_EqualLegs_Review.blend'
review.REPORT = json.loads((review.SOURCE/'WarMachine_handbuilt_report.json').read_text(encoding='utf8'))
review.SCALE = review.REPORT['source_scale']
review.OFFSET = Vector(review.REPORT['source_offset'])
review.REVISION_DESCRIPTION = 'v4: front arms shortened to rear arm length at 30 degrees; front feet move inward and upward, all discs horizontal'


def beam_axis(corner):
    beam = bpy.data.objects['WM suspension diagonal_'+corner]
    pin = bpy.data.objects['WM suspension cross pin_'+corner]
    start = sum((v.co for v in list(beam.data.vertices)[:4]), Vector())/4
    pivot = sum((v.co for v in pin.data.vertices), Vector())/len(pin.data.vertices)
    delta = pivot-start
    return {'corner':corner, 'length_m':delta.length,
            'start_m':list(start), 'end_pin_m':list(pivot),
            'down_degrees':math.degrees(math.atan2(-delta.z,delta.xy.length))}


def prepare():
    bpy.ops.wm.open_mainfile(filepath=str(review.SOURCE_EDITABLE))
    before = review.mesh_parts()
    initial = {o.name:review.signature(o) for o in before}
    old_vertices = {o.name:[v.co.copy() for v in o.data.vertices] for o in before}
    old_axes = [beam_axis(c) for c in ('F_L','F_R','R_L','R_R')]
    lo0,hi0,_,_ = author.evaluated_stats(before)
    delta_left = design.hover_foot_offset('F')*review.SCALE
    shifts = {'F_L':delta_left, 'F_R':Vector((delta_left.x,-delta_left.y,delta_left.z))}
    changed = [o.name for o in before if '_F_' in o.name and o.name.startswith(design.CONNECTION_PREFIXES)]
    moved_feet = [o.name for o in before if '_F_' in o.name and o.name.startswith('WM hover ')]
    assert len(changed)==32 and len(moved_feet)==28
    for name in moved_feet:
        corner = 'F_L' if '_F_L' in name else 'F_R'
        bpy.data.objects[name].data.transform(Matrix.Translation(shifts[corner]))
    for name in changed:
        bpy.data.objects.remove(bpy.data.objects[name],do_unlink=True)
    author.PARTS=[]
    design.build_hover_leg_connection(author,'F',2.75,.63)
    for obj in list(author.PARTS):
        author.mirror(obj)
    transform = Matrix.Scale(review.SCALE,4)@Matrix.Translation(review.OFFSET)
    for obj in author.PARTS:
        obj.data.transform(transform)
        for mod in obj.modifiers:
            if mod.type=='BEVEL':
                mod.width *= review.SCALE
    bpy.context.view_layer.update()
    after = review.mesh_parts()
    dg=bpy.context.evaluated_depsgraph_get()
    untouched=[name for name in initial if name not in changed and name not in moved_feet]
    untouched_identical=all(review.signature(bpy.data.objects[n])==initial[n] for n in untouched)
    moved = moved_feet+[n for n in changed if not n.startswith('WM suspension diagonal_')]
    move_error=0
    for name in moved:
        corner='F_L' if '_F_L' in name else 'F_R'
        obj=bpy.data.objects[name]
        assert len(obj.data.vertices)==len(old_vertices[name])
        move_error=max(move_error,max((v.co-old-shifts[corner]).length
                       for v,old in zip(obj.data.vertices,old_vertices[name])))
    axes=[beam_axis(c) for c in ('F_L','F_R','R_L','R_R')]
    contacts,holes,interference,invalid=[],[],[],[]
    for corner in ('F_L','F_R','R_L','R_R'):
        cap=bpy.data.objects['WM hover hub cap_'+corner]
        seat=bpy.data.objects['WM suspension hub seat_'+corner]
        cv,_=slope.evaluated(cap,dg)
        sv,_=slope.evaluated(seat,dg)
        center_cap=sum(cv,Vector())/len(cv)
        center_seat=sum(sv,Vector())/len(sv)
        raw_top=max(v.co.z for v in cap.data.vertices)
        flat_top=[v.co.z for v in cap.data.vertices if abs(v.co.z-raw_top)<1e-4]
        contacts.append({'corner':corner,'gap_m':min(v.z for v in sv)-max(v.z for v in cv),
                         'center_error_xy_m':(center_cap-center_seat).xy.length,
                         'cap_top_m':max(v.z for v in cv),'disc_top_normal':[0,0,1],
                         'cap_flatness_error_m':max(flat_top)-min(flat_top)})
        axis_record=next(a for a in axes if a['corner']==corner)
        pivot=Vector(axis_record['end_pin_m'])
        axis=pivot-Vector(axis_record['start_m'])
        across=Vector((-axis.y,axis.x,0)).normalized()
        pin=bpy.data.objects['WM suspension cross pin_'+corner]
        radius=max(((v.co-pivot)-across*(v.co-pivot).dot(across)).length for v in pin.data.vertices)
        names=['WM suspension pivot eye_'+corner,'WM suspension clevis lug_'+corner+' inner',
               'WM suspension clevis lug_'+corner+' outer']
        for name in names:
            ring=[v.co for v in bpy.data.objects[name].data.vertices][40:60]
            center=sum(ring,Vector())/len(ring)
            delta=center-pivot
            error=(delta-across*delta.dot(across)).length
            clearance=min(((v-pivot)-across*(v-pivot).dot(across)).length for v in ring)-radius
            holes.append({'name':name,'axis_error_m':error,'radial_clearance_m':clearance})
        discs=[o for o in after if o.name.startswith('WM hover ') and corner in o.name]
        legs=[o for o in after if o.name.startswith(design.CONNECTION_PREFIXES) and corner in o.name]
        for leg in legs:
            lv,lf=slope.evaluated(leg,dg)
            lt=BVHTree.FromPolygons(lv,lf)
            for disc in discs:
                if leg==seat and disc==cap:
                    continue
                dv,df=slope.evaluated(disc,dg)
                count=len(lt.overlap(BVHTree.FromPolygons(dv,df)))
                if count:
                    interference.append({'leg':leg.name,'disc':disc.name,'triangle_pairs':count})
    # Front feet move closer to the hull; explicitly check that new placement.
    hull=[o for o in after if not o.name.startswith('WM hover ') and not o.name.startswith('WM suspension ') and not o.name.startswith('WM toe amber optic_')]
    foot_hull=[]
    for name in moved_feet:
        fv,ff=slope.evaluated(bpy.data.objects[name],dg)
        ft=BVHTree.FromPolygons(fv,ff)
        for obj in hull:
            hv,hf=slope.evaluated(obj,dg)
            count=len(ft.overlap(BVHTree.FromPolygons(hv,hf)))
            if count:
                foot_hull.append({'disc':name,'hull':obj.name,'triangle_pairs':count})
    for name in changed:
        bm=bmesh.new()
        bm.from_mesh(bpy.data.objects[name].data)
        if any(not e.is_manifold for e in bm.edges) or bm.calc_volume(signed=True)<=0:
            invalid.append(name)
        bm.free()
    lo1,hi1,tris,symmetry=author.evaluated_stats(after)
    length_error=max(a['length_m'] for a in axes)-min(a['length_m'] for a in axes)
    root_error=max((Vector(a['start_m'])-Vector(b['start_m'])).length for a,b in zip(axes,old_axes))
    report={'source':str(review.SOURCE_EDITABLE),'editable':str(review.EDITABLE),'parts':len(after),
            'triangles':tris,'axes_before':old_axes,'axes_after':axes,'four_arm_length_spread_m':length_error,
            'body_side_start_max_delta_m':root_error,'front_shortening_percent':100*(1-axes[0]['length_m']/old_axes[0]['length_m']),
            'front_foot_offset_author_units':list(design.hover_foot_offset('F')),
            'front_foot_offset_m':{c:list(v) for c,v in shifts.items()},
            'untouched_parts_count':len(untouched),'untouched_parts_identical':untouched_identical,
            'rigidly_moved_parts_count':len(moved),'rigid_translation_max_error_m':move_error,
            'contacts':contacts,'pin_holes':holes,'leg_disc_surface_intersections':interference,
            'moved_disc_hull_surface_intersections':foot_hull,'non_manifold_or_inward_rebuilt_parts':invalid,
            'symmetry_error_m':symmetry,'bounds_before_m':[list(lo0),list(hi0)],'bounds_after_m':[list(lo1),list(hi1)],
            'sockets_m':review.REPORT['sockets_m'],'body_and_weapons_unchanged':True,
            'art_review':'pending_user_review','ue_import':'not_performed','card_generation':'waiting_for_model_approval'}
    review.write_json(review.OUT/'geometry-check.json',report)
    assert len(after)==279 and len(untouched)==219 and len(moved)==58
    assert untouched_identical and move_error<1e-4
    assert length_error<1e-4 and root_error<1e-4 and symmetry<1e-4
    assert all(abs(a['down_degrees']-30)<1e-4 for a in axes)
    assert all(abs(c['gap_m'])<1e-4 and c['center_error_xy_m']<1e-4 for c in contacts)
    assert all(h['axis_error_m']<1e-4 and h['radial_clearance_m']>0 for h in holes)
    assert not interference and not foot_hull and not invalid
    # Front-most disc edge retracts with its foot; body height and rear/side
    # extremities remain unchanged. Overall length need not stay fixed.
    assert (lo1-lo0).length<1e-4 and abs(hi1.y-hi0.y)<1e-4 and abs(hi1.z-hi0.z)<1e-4
    assert hi1.x<hi0.x
    bpy.context.scene.name='WarMachine_EqualLegs_Editable'
    bpy.context.scene['revision']=review.REVISION_DESCRIPTION
    bpy.context.scene['approval']='Pending user model review; no UE import'
    bpy.context.scene['suspension_down_degrees']=30.0
    bpy.context.scene['all_four_arms_equal_length']=True
    bpy.context.scene['front_feet_higher']=True
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(review.EDITABLE),check_existing=False)
    review.write_json(review.OUT/'WarMachine_handbuilt_report.json',dict(review.REPORT,
        bounds_m=[list(lo1),list(hi1)],parts=len(after),triangles=tris,symmetry_error_m=symmetry,
        support_length_author_units=design.REAR_SUPPORT_LENGTH,
        front_foot_offset_author_units=list(design.hover_foot_offset('F')),design_revision=review.REVISION_DESCRIPTION))
    review.log('v4 saved: four equal-length 30-degree arms; 219 parts unchanged; front disc contacts and hull clearance checked')


def main():
    review.OUT.mkdir(parents=True,exist_ok=True)
    review.PREVIEWS.mkdir(exist_ok=True)
    stage=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    if stage in ('prepare','all'):
        prepare()
    if stage in ('render','all'):
        review.package_review('Before')
        review.package_review('After')
        review.comparison()
        slope.side_comparison('v3 / Before','v4 / Equal supports')
    review.log('EqualLegs completed '+stage)


if __name__=='__main__':
    main()

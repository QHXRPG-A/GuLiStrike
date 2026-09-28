"""v5: tilt front clevis/ankles, retain equal beams, level all four discs.

User owns visual inspection. This script writes final model previews but does
not open or analyse them. Validation is geometry-only.
"""
import bpy
import bmesh
import json
import math
import sys
from pathlib import Path
from mathutils import Matrix,Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0,str(Path(__file__).parent))
import revise_warmachine_equal_legs as equal
review,design,author=equal.review,equal.design,equal.author
review.SOURCE=review.ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_EqualLegs_v4'
review.SOURCE_EDITABLE=review.SOURCE/'WarMachine_EqualLegs_Editable.blend'
review.OUT=review.ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelFeet_v5'
review.PREVIEWS=review.OUT/'Previews'
review.EDITABLE=review.OUT/'WarMachine_LevelFeet_Editable.blend'
review.REVIEW=review.OUT/'WarMachine_LevelFeet_Review.blend'
review.REPORT=json.loads((review.SOURCE/'WarMachine_handbuilt_report.json').read_text(encoding='utf8'))
review.SCALE=review.REPORT['source_scale']
review.OFFSET=Vector(review.REPORT['source_offset'])
review.REVISION_DESCRIPTION='v5: downward front clevis/ankles and compensating bearing seats; all horizontal hover discs on the rear plane; equal beams unchanged'
review.CAMERA_SPECS=[c for c in review.CAMERA_SPECS if c[0] in ('Hero','Full_Front','Full_Side','Connection_Oblique')]


def prepare():
    bpy.ops.wm.open_mainfile(filepath=str(review.SOURCE_EDITABLE))
    before=review.mesh_parts()
    initial={o.name:review.signature(o) for o in before}
    vertices={o.name:[v.co.copy() for v in o.data.vertices] for o in before}
    old_axes=[equal.beam_axis(c) for c in ('F_L','F_R','R_L','R_R')]
    old_shift=Vector(review.REPORT['front_foot_offset_author_units'])
    shift=(design.hover_foot_offset('F')-old_shift)*review.SCALE
    shifts={'F_L':shift,'F_R':Vector((shift.x,-shift.y,shift.z))}
    rebuilt=[o.name for o in before if '_F_' in o.name and o.name.startswith(design.CONNECTION_PREFIXES)]
    feet=[o.name for o in before if '_F_' in o.name and o.name.startswith('WM hover ')]
    assert len(rebuilt)==32 and len(feet)==28
    for name in feet:
        bpy.data.objects[name].data.transform(Matrix.Translation(shifts['F_L' if '_F_L' in name else 'F_R']))
    for name in rebuilt:
        bpy.data.objects.remove(bpy.data.objects[name],do_unlink=True)
    author.PARTS=[]
    design.build_hover_leg_connection(author,'F',2.75,.63)
    for obj in list(author.PARTS):
        author.mirror(obj)
    transform=Matrix.Scale(review.SCALE,4)@Matrix.Translation(review.OFFSET)
    for obj in author.PARTS:
        obj.data.transform(transform)
        for mod in obj.modifiers:
            if mod.type=='BEVEL':
                mod.width *= review.SCALE
    bpy.context.view_layer.update()
    after=review.mesh_parts()
    untouched=[n for n in initial if n not in rebuilt and n not in feet]
    identical=all(review.signature(bpy.data.objects[n])==initial[n] for n in untouched)
    retained_front=[n for n in rebuilt if review.signature(bpy.data.objects[n])==initial[n]]
    axes=[equal.beam_axis(c) for c in ('F_L','F_R','R_L','R_R')]
    axis_error=max((Vector(a['start_m'])-Vector(b['start_m'])).length+(Vector(a['end_pin_m'])-Vector(b['end_pin_m'])).length for a,b in zip(axes,old_axes))
    dg=bpy.context.evaluated_depsgraph_get()
    contacts,holes,intersections,invalid=[],[],[],[]
    evalmesh=equal.slope.evaluated
    for corner in ('F_L','F_R','R_L','R_R'):
        cap=bpy.data.objects['WM hover hub cap_'+corner]
        seat=bpy.data.objects['WM suspension hub seat_'+corner]
        cv,_=evalmesh(cap,dg)
        sv,_=evalmesh(seat,dg)
        ccenter=sum(cv,Vector())/len(cv)
        low=min(v.z for v in sv)
        bottom=[v for v in sv if abs(v.z-low)<1e-4]
        scenter=sum(bottom,Vector())/len(bottom)
        contacts.append({'corner':corner,'gap_m':low-max(v.z for v in cv),'base_center_error_xy_m':(ccenter-scenter).xy.length,
                         'cap_top_m':max(v.z for v in cv),'disc_axis':[0,0,1]})
        a=next(a for a in axes if a['corner']==corner)
        pivot=Vector(a['end_pin_m']);beam=pivot-Vector(a['start_m'])
        across=Vector((-beam.y,beam.x,0)).normalized()
        pin=bpy.data.objects['WM suspension cross pin_'+corner]
        radius=max(((v.co-pivot)-across*(v.co-pivot).dot(across)).length for v in pin.data.vertices)
        for name in ['WM suspension pivot eye_'+corner,'WM suspension clevis lug_'+corner+' inner','WM suspension clevis lug_'+corner+' outer']:
            ring=[v.co for v in bpy.data.objects[name].data.vertices][40:60]
            center=sum(ring,Vector())/len(ring);delta=center-pivot
            holes.append({'name':name,'axis_error_m':(delta-across*delta.dot(across)).length,
                          'radial_clearance_m':min(((v-pivot)-across*(v-pivot).dot(across)).length for v in ring)-radius})
        discs=[o for o in after if o.name.startswith('WM hover ') and corner in o.name]
        legs=[o for o in after if o.name.startswith(design.CONNECTION_PREFIXES) and corner in o.name]
        for leg in legs:
            lv,lf=evalmesh(leg,dg);lt=BVHTree.FromPolygons(lv,lf)
            for disc in discs:
                if leg==seat and disc==cap:
                    continue
                dv,df=evalmesh(disc,dg)
                count=len(lt.overlap(BVHTree.FromPolygons(dv,df)))
                if count:
                    intersections.append({'leg':leg.name,'disc':disc.name,'triangle_pairs':count})
    for name in rebuilt:
        bm=bmesh.new();bm.from_mesh(bpy.data.objects[name].data)
        if any(not e.is_manifold for e in bm.edges) or bm.calc_volume(signed=True)<=0:
            invalid.append(name)
        bm.free()
    translation_error=max((v.co-old-shifts['F_L' if '_F_L' in name else 'F_R']).length
                          for name in feet for v,old in zip(bpy.data.objects[name].data.vertices,vertices[name]))
    lo,hi,tris,sym=author.evaluated_stats(after)
    plane_error=max(c['cap_top_m'] for c in contacts)-min(c['cap_top_m'] for c in contacts)
    data={'source':str(review.SOURCE_EDITABLE),'editable':str(review.EDITABLE),'parts':len(after),'triangles':tris,
          'front_connector_down_degrees':math.degrees(design.front_connector_angle()),
          'front_foot_offset_author_units':list(design.hover_foot_offset('F')),
          'front_foot_move_from_v4_m':{c:list(v) for c,v in shifts.items()},'axes':axes,
          'beam_axes_max_change_m':axis_error,'four_arm_length_spread_m':max(a['length_m'] for a in axes)-min(a['length_m'] for a in axes),
          'four_disc_height_spread_m':plane_error,'contacts':contacts,'pin_holes':holes,
          'leg_disc_surface_intersections':intersections,'invalid_rebuilt_meshes':invalid,
          'untouched_count':len(untouched),'untouched_identical':identical,'regenerated_identical_front_parts':retained_front,
          'disc_translation_error_m':translation_error,'symmetry_error_m':sym,'bounds_m':[list(lo),list(hi)],
          'sockets_m':review.REPORT['sockets_m'],'art_review':'pending_user_review',
          'assistant_visual_inspection':'not_performed_per_user_instruction','ue_import':'not_performed','card_generation':'waiting_for_model_approval'}
    review.write_json(review.OUT/'geometry-check.json',data)
    assert len(after)==279 and identical and axis_error<1e-4
    assert plane_error<1e-4 and sym<1e-4 and translation_error<1e-4
    assert all(abs(c['gap_m'])<1e-4 and c['base_center_error_xy_m']<1e-4 for c in contacts)
    assert all(h['axis_error_m']<1e-4 and h['radial_clearance_m']>0 for h in holes)
    assert not intersections and not invalid
    bpy.context.scene.name='WarMachine_LevelFeet_Editable'
    bpy.context.scene['revision']=review.REVISION_DESCRIPTION
    bpy.context.scene['approval']='Pending user inspection; assistant did not view previews'
    bpy.context.scene['front_connector_down_degrees']=data['front_connector_down_degrees']
    bpy.context.scene['four_discs_same_plane']=True
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.wm.save_as_mainfile(filepath=str(review.EDITABLE),check_existing=False)
    review.write_json(review.OUT/'WarMachine_handbuilt_report.json',dict(review.REPORT,
        parts=len(after),triangles=tris,bounds_m=[list(lo),list(hi)],symmetry_error_m=sym,
        front_connector_down_degrees=data['front_connector_down_degrees'],
        front_foot_offset_author_units=list(design.hover_foot_offset('F')),design_revision=review.REVISION_DESCRIPTION))
    review.log('v5 saved: front connectors tilted; four discs coplanar; unchanged equal beams checked numerically')


def main():
    review.OUT.mkdir(parents=True,exist_ok=True);review.PREVIEWS.mkdir(exist_ok=True)
    stage=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    if stage in ('prepare','all'):
        prepare()
    if stage in ('render','all'):
        review.package_review('After')
    review.log('LevelFeet completed '+stage)


if __name__=='__main__':
    main()

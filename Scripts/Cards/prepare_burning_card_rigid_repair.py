"""Prepare four targeted edits for one-object ownership and an uncut ring."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/BurningCards_HTML_20261029_v1'
REPAIR=OUT/'Repairs/RigidObjects_v4'

EDITS={
 'Burst_3':'''Redraw the shockwave as ONE COMPLETE UNBROKEN ELLIPSE, entirely visible inside this square PNG with generous genuinely transparent margin on ALL FOUR sides. It must be a strongly foreshortened wide ellipse: about 82% of canvas width and 28% of canvas height, centered in the canvas. Both left and right turning ends must be drawn completely, smoothly joining the near and far arcs. No cropped ends, vertical clipping planes, rectangular gaps, disconnected straight beams, robot silhouette holes or division into separate images. Keep the selected reference's orange/gold cel-shaded illustrated style, thin bright outer leading edge and tapered outward flame tongues to indicate outward propagation. Keep flame tongues short enough to fit within the transparent margins. This is a ring emitted at the central robot's TORSO HEIGHT, not at the feet. The complete ring asset will be positioned and naturally clipped by the final card window. Do not add robots, debris, terrain, annotations, arrows or text. Transparent space inside the ellipse is intentional; gaps in the ring itself are not.''',
 'Burst_2':'''Keep only the CLOSE FOREGROUND GRAY STONE OBJECTS as complete isolated solid boulders, near the lower-left and bottom-center of the shared composition. Repaint their obscured stone facets coherently. Remove ALL grass, soil, connected ground strips, distant terraces, distant buildings, other stones, fire and smoke. Remove all red/burgundy/ivory mechanical armor: the large burning ivory/burgundy foreground shard is a DIFFERENT object owned by layer 1 and MUST NOT appear in this layer. Each gray stone belongs only to this layer. Do not create a second copy of any foreground mechanical shard, robot leg, gun, antenna or terrain surface. The surrounding canvas is real alpha=0, including the regions between separate boulders. Preserve the reference's slate-gray faceted stone, clean outlines and three-tone shading. Preserve the existing approximate bottom-edge composition; do not turn the stones into a continuous full-width grassy terrace.''',
 'Burst_6':'''This is the CLEAN LANDSCAPE BACKGROUND, and all pixels must remain opaque. Remove the CLOSE large gray foreground boulders, near-camera rock faces and frontmost stone slab along the bottom quarter, replacing their whole silhouettes with continuous gently sloping grass/soil. Those close individual stones belong exclusively to another foreground layer; do not leave ghost copies or remnants of them here. Keep the distant and middle-distance stone terraces, ruined arches/towers, tilted perspective, blue sky and clouds. Retain the original selected colors and flat three-tone linework. No machines, bullets, foreground metallic/armor shards, airborne debris, fire, shockwave lines or muzzle flashes. Reconstruct a clean continuous grassy/earth surface under all removed near-camera objects, with no silhouette cutouts, rectangular patches, blurred cloning or repeated edge smears. Do not redesign the distant architecture or camera angle.''',
 'Bullet_2':'''Keep ONLY the INDEPENDENT FREE-FLYING SMALL DEBRIS and the THREE PROMINENT OUTER ARMOR SHARDS: the large lower-left burgundy/ivory foreground piece, the bottom-edge separate armor plate and the upper-right cropped armor piece. Restore each shard's complete solid faces and its own attached burning flames. All these objects belong solely to this debris layer. REMOVE every articulated robot leg/foot, connected multi-joint limb, torso shell assembly, large inner machinery assembly, complete gun/gun mount and antenna. All connected central enemy structure belongs exclusively to layer 4; it MUST NOT appear here as a duplicate of the same robot. Keep the central body region largely transparent, with at most sparse tiny independent charred chips; no large structural parts there. Do not add a robot, projectile, independent impact fireball, tracer, floor, grass or architecture. Keep the same clear linework and flat three-tone colors. Preserve the edge-based layout and the attached flames of each individual shard; do not place disconnected fire in a second layer or crop straight stripes through a shard.''',
}


def main():
    REPAIR.mkdir(parents=True,exist_ok=True)
    for name in ['Inputs','Prompts','Raw']:(REPAIR/name).mkdir(exist_ok=True)
    previous=json.loads((REPAIR/'previous-generation-requests.json').read_text(encoding='utf-8'))
    prior_results=json.loads((REPAIR/'previous-generation-results.json').read_text(encoding='utf-8'))
    active={r['id']:r for r in prior_results if r.get('status')=='active'}
    requests=[];reused=[]
    for old in previous:
        req=dict(old);key=req['id']
        if key in EDITS:
            original=Path(active[key]['output']);edit=REPAIR/'Inputs'/f'{key}_Edit.png'
            if not edit.exists():shutil.copy2(original,edit)
            reference=OUT/req['card']/'Master.png'
            prompt=('Use case: precise-object-edit / transparent layered game-card art.\n'
                'Image 1 is the exact edit target. Image 2 is the selected original card for design and color context only.\n'
                'This revision fixes the user-reported cut shockwave and duplicated physical objects. A physical object, its material, outlines and attached fire must be drawn completely in ONE owning layer. Never split or duplicate pieces of the same object into other layers.\n'
                +EDITS[key]+'\nNo UI, frame, labels, watermark or checkerboard. '+
                ('Return a completely opaque full-canvas PNG.' if req['index']==6 else 'Return real RGBA PNG, alpha=0 outside the requested objects.'))
            file=REPAIR/'Prompts'/f'{key}.txt';file.write_text(prompt,encoding='utf-8')
            req.update(prompt=prompt,referenced_image_paths=[edit.as_posix(),reference.as_posix()],
                input_roles=['targeted repair of current painted layer','selected original design context'],
                input_sha256=[hashlib.sha256(p.read_bytes()).hexdigest() for p in [edit,reference]],
                prompt_file=file.relative_to(OUT).as_posix(),output=(REPAIR/'Raw'/f'{key}.png').as_posix(),
                transparent_background=req['index']!=6,revision=4)
        else:
            record=dict(active[key],reused_from_revision=3);reused.append(record)
        requests.append(req)
    (REPAIR/'requests.json').write_text(json.dumps(requests,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    destination=REPAIR/'results.json'
    if not destination.exists():destination.write_text(json.dumps(reused,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'new_edit_requests':list(EDITS),'retained_layers':len(reused)},ensure_ascii=False))


if __name__=='__main__':main()

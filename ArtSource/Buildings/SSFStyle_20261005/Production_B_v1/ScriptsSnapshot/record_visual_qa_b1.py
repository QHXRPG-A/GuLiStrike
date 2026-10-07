"""Record the assistant's completed read-image inspection separately from user B."""
import json,hashlib
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
record={'version':'SSF_Production_B_v1','date':'2026-10-05','source_blend_sha256':hashlib.sha256((O/'SSF_Production_B_v1.blend').read_bytes()).hexdigest(),
 'assistant_visual_review_completed':True,'user_B_approval':False,'strict_visual_equivalence_user_approved':False,
 'inspected_evidence':{'core_views':'All 40 Hero/Front/Left/Back images through 3 actual contact sheets; Factory and Strategy far views separately enlarged',
 'top_views':['Renders/Floor_Top.png','Renders/Drone_Top.png'],'compositions':['Renders/Assembly_Hero.png','Renders/Assembly_Top.png'],
 'reference_comparison':'All ten native Hero comparisons; Factory Hero/Front comparison enlarged',
 'LODs':'All ten assets three fixed-camera LOD heroes through 3 actual comparison pages',
 'gray_normals':'All ten actual normal/geometry diagnostic heroes through 3 contact pages',
 'style':'Actual Factory flat color, line/shell, three-tone and combined native renders',
 'movies':'All 22 delivered MP4s read back by the native decoder; assistant inspected the 4 actual start/middle/end contact pages (66 decoded checkpoints), plus enlarged Factory open-door middle and AirBase destruction middle. Automatic pixel comparison covers all 110 checkpoints; no claim of viewing every video frame.'},
 'observations':['Six distinct A_v7 schemes retained; only the previously requested Factory/Strategy dark roles stay lightened. Other four schemes and props retain their approved colors.',
 'Source silhouettes, original assembly and repeated mechanical fittings remain; all production groups survive every LOD. Source functional asymmetry is preserved.',
 'Curved AirBase and Reactor profiles remain; the corrected far antennas have no prior decimation spikes or orange team-color bleed.',
 'Gray views no longer have the earlier large custom-normal patches. Native technical checks verify every corner normal finite/unit and every metal vertex rigid.',
 'Open-door and destruction previews expose retained interiors and fragments consistently at all LODs. Cloning hose motion retains source flex weights; Root/hierarchy and source motion are technically compared separately.',
 'Original Light is a single-sided two-triangle translucent plane; invisible Left/Back orthographic views are intentional. The reference helper plane border is not a physical product part.',
 'Actual line mask and three-band shading are visually present in the controlled native breakdown. Far LOD fades fine linework and removes hulls.'],
 'remaining_differences':[{'item':'Body triangle budgets','status':'pending_user_exception_decision','description':'24 body LODs exceed their original caps to preserve every animated part and source profiles; budgets have not been silently changed.'},
 {'item':'Linework and outline coverage','status':'pending_user_B_decision','description':'Budget-limited real skinned hulls cover principal opaque components; some exterior strokes are thinner/weaker than A_v7 Freestyle. Reprojected panel-line details differ locally, notably the simplified clean factory shutter seams. Side-by-side images show this explicitly; no strict visual match approval is claimed.'},
 {'item':'UE equivalence and performance','status':'not_run','description':'Formal FBX roundtrip, UE material/LOD references, physical/BP copies, commander cameras and in-game frame rate remain after user B.'}],
 'limits':'Assistant image inspection and native geometry/motion checks do not grant user B or budget exceptions.'}
(O/'visual_qa.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_ASSISTANT_VISUAL_QA_RECORDED',flush=True)

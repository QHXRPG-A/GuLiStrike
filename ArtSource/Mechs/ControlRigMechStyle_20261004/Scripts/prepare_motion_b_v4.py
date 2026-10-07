from pathlib import Path
import shutil
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');O=R/'Production_B_v4';S=R/'Scripts'
shutil.copytree(R/'Production_B_v3/AnimationSource',O/'AnimationSource',dirs_exist_ok=True)
text=(S/'render_motion_b_v3.py').read_text(encoding='utf-8').replace('B_v3','B_v4').replace('B-v3','B-v4').replace('realtime_readback_audit.json','line_revision_report.json').replace('saved_body_readback_report_sha256','line_revision_report_sha256')
(S/'render_motion_b_v4.py').write_text(text,encoding='utf-8')
text=(S/'verify_movies_b_v3.py').read_text(encoding='utf-8').replace('B_v3','B_v4').replace('B-v3','B-v4')
start=text.index("    folder=R/'Logs'/'FullMovieReadback'/clip")
end=text.index('    results.append(',start)
text=text[:start]+text[end:]
(S/'verify_movie_checkpoints_b_v4.py').write_text(text,encoding='utf-8')
print('B4 full motion and checkpoint readback workflow prepared')

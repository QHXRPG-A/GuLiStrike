from pathlib import Path
source=Path(__file__).with_name('render_a4_review.py').read_text(encoding='utf8')
source=source.replace('Production_A4_B_v1','Production_A4_B_v2').replace('GroundMech_A4_','GroundMech_A4B2_')
source=source.replace("'GroundMech_A4B2_03_SingleInspect']", "'GroundMech_A4B2_03_SingleInspect','GroundMech_A4B2_04_ChestAssembly','GroundMech_A4B2_05_HeelAssembly','GroundMech_A4B2_06_ToeAssembly','GroundMech_A4B2_07_WeaponAssembly','GroundMech_A4B2_08_LegAssembly']")
exec(compile(source,__file__,'exec'),globals())

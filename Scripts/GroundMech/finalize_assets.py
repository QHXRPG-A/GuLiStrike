"""Dedicated authoring worker; no live PIE worlds are held during asset reimport."""
from pathlib import Path
root=Path('D:/UE5.7/test1/Scripts/GroundMech')
exec(compile((root/'import_assets.py').read_text(encoding='utf-8'),'ground_import','exec'))
exec(compile((root/'inspect_assets.py').read_text(encoding='utf-8'),'ground_validate','exec'))
exec(compile((root/'build_demo.py').read_text(encoding='utf-8'),'ground_demo','exec'))

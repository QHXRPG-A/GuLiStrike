"""Compatibility entry for current Commander three-tier candidates."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path('D:/UE5.7/test1/Scripts/CommanderLOD')))
from legacy_entry import run
run('inspect_candidates.py','unreal')

"""Compatibility entry: camera helpers only; production references are already saved."""
from pathlib import Path
import unreal
_review_script=Path(unreal.Paths.project_dir())/"Scripts/Performance/client_three_player_review.py"
exec(compile(_review_script.read_text(encoding="utf-8"),str(_review_script),"exec"),globals())

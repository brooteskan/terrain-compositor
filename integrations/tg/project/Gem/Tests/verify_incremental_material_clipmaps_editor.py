"""Entry point; maintained by the terrain-compositor submodule."""
from pathlib import Path
import runpy
_script=Path(__file__).resolve().parents[2]/'Gems/terrain-compositor/tests/editor/verify_incremental_material_clipmaps_editor.py'
globals().update(runpy.run_path(str(_script),run_name=__name__))

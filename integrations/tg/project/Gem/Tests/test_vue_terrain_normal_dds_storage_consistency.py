"""Entry point; maintained by the terrain-compositor submodule."""
from pathlib import Path
import runpy
_script=Path(__file__).resolve().parents[2]/'Gems/terrain-compositor/tests/tg/test_vue_terrain_normal_dds_storage_consistency.py'
globals().update(runpy.run_path(str(_script),run_name=__name__))

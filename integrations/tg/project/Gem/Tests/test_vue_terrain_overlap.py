# Canonical test source belongs to the terrain-compositor submodule.
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[2]/'Gems/terrain-compositor/tests/tg/test_vue_terrain_overlap.py'), run_name='__main__')

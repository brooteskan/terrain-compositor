"""Numerical storage and page-border contracts for the offline prototype."""
import importlib.util, tempfile, unittest
from pathlib import Path
import numpy as np

source=Path(__file__).with_name('BuildOverlapPages.py')
if not source.exists(): source=Path(__file__).resolve().parents[2]/'tools/BuildOverlapPages.py'
spec=importlib.util.spec_from_file_location('pages',source)
pages=importlib.util.module_from_spec(spec);spec.loader.exec_module(pages)

class PageContracts(unittest.TestCase):
 def test_half_storage_roundtrip_and_exact_budget(self):
  a=np.arange(8*8*4,dtype=np.float32).reshape(8,8,4)/256
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'page.dds';pages.dds(p,a)
   np.testing.assert_array_equal(pages.read(p),a.astype(np.float16).astype(np.float32))
   self.assertEqual(p.stat().st_size,148+8*8*8)
 def test_control_field_sample_centers(self):
  a=np.arange(4,dtype=np.float32).reshape(2,2,1)
  np.testing.assert_array_equal(pages.sample(a,np.array([[0,0],[1,1],[.5,.5]]),True)[:,0],[0,3,1.5])
 def test_filtering_across_page_border_reproduces_linear_field(self):
  for pitch in (64,128,256):
   core=pitch-2;size=4*pitch
   logical=np.concatenate([np.arange(core+2)-.5+i*core for i in range(4)])
   atlas=np.broadcast_to(logical[None,:,None],(2,size,1))
   positions=np.array([core-.001,core,core+.001,2*core-.25,2*core+.25])
   page=np.floor(positions/core);physical=page*pitch+(positions/core-page)*core+1
   uv=np.stack([physical/size,np.full(len(positions),.5)],-1)
   np.testing.assert_allclose(pages.sample(atlas,uv)[:,0],positions,atol=1e-4)
 def test_wrapped_material_and_clamped_height_are_distinct(self):
  a=np.array([[[0],[1]]],np.float32);uv=np.array([[1.25,.5]])
  self.assertAlmostEqual(float(pages.sample(a,uv,wrap=True)[0,0]),0)
  self.assertAlmostEqual(float(pages.sample(a,uv,wrap=False)[0,0]),1)

if __name__=='__main__':unittest.main()

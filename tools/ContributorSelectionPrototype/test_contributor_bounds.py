import importlib.util
from pathlib import Path
import unittest
import numpy as np

spec=importlib.util.spec_from_file_location('tiles',Path(__file__).with_name('BuildContributorTiles.py'))
t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)

class Bounds(unittest.TestCase):
 def test_transformed_bounds_cover_samples(self):
  rng=np.random.default_rng(94)
  a=rng.uniform(-3,12,(19,23))
  for angle in (0,.7,1.9):
   c,s=np.cos(angle),np.sin(angle)
   source={'uvRow0':[c*.3,-s*.3,.4],'uvRow1':[s*.3,c*.3,.5],'heightScaleOffset':[-2,7]}
   lo=np.array([-.3,-.4]);hi=np.array([.6,.7])
   b=t.tile_source_bounds(source,a,lo,hi,.03)
   world=rng.uniform(lo-.03,hi+.03,(20000,2))
   uv=np.c_[world,np.ones(len(world))]@np.array([source['uvRow0'],source['uvRow1']]).T
   values=t.bilinear(a,uv)*-2+7
   self.assertLessEqual(b['lo'],values.min());self.assertGreaterEqual(b['hi'],values.max())
 def test_uncertain_seam_and_tiny_difference(self):
  b={'missing':False,'lo':20,'hi':20.1,'edge_distance':0}
  self.assertEqual(t.classify([b,b],10,.001),0)
  full=dict(b,edge_distance=20)
  self.assertEqual(t.classify([full,dict(full,lo=29.99,hi=30.01)],10,.001),0)
 def test_proven_cases(self):
  b={'missing':False,'lo':20,'hi':20.1,'edge_distance':20}
  self.assertEqual(t.classify([b,b],10,.001),2)
  self.assertEqual(t.classify([b,dict(b,lo=40,hi=41)],10,.001),1)
  self.assertEqual(t.classify([b,{'missing':True}],10,.001),1)

if __name__=='__main__':unittest.main()

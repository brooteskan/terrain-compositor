import os as _os
from pathlib import Path as _Path
PROJECT = _Path(_os.environ['TG_PROJECT_ROOT']).resolve() if 'TG_PROJECT_ROOT' in _os.environ else next(
    p for p in _Path(__file__).resolve().parents if (p/'project.json').is_file())
"""Offline scalar-model regression for the intended full-strength shortcut.

The isolated runner snapshots Python files only. This model does not read,
execute, or verify the current shader. Source equivalence, shader compilation,
appearance, and performance require separate evidence.
"""
import math
import random
import unittest

def normalize(v):
    length = math.sqrt(sum(x*x for x in v))
    if not length: raise ValueError('zero normal')
    return tuple(x/length for x in v)

def lerp(a,b,t):
    return tuple(x+(y-x)*t for x,y in zip(a,b))

class NormalShortcutTests(unittest.TestCase):
    def candidate(self,incoming,smooth,strength):
        # Explicit intended arithmetic model, not a shader-source binding.
        if strength<=0 or sum(x*x for x in smooth)<=0.0001: return incoming
        weight=min(1,strength)
        if weight>=1:
            return normalize(smooth)
        return normalize(lerp(normalize(incoming),normalize(smooth),weight))

    def test_full_and_fractional_strength_match_baseline_for_nonunit_normals(self):
        rng=random.Random(8431)
        for _ in range(250):
            incoming=tuple(rng.uniform(-4,4) for _ in range(3))
            smooth=tuple(rng.uniform(-4,4) for _ in range(3))
            for strength in (0.01,0.25,0.5,0.99,1,2):
                expected=normalize(lerp(normalize(incoming),normalize(smooth),min(1,strength)))
                actual=self.candidate(incoming,smooth,strength)
                for a,b in zip(actual,expected): self.assertAlmostEqual(a,b,places=12)

    def test_filtered_normal_counterexample_and_positive_scale_invariance(self):
        incoming=(0,0,1); smooth=(0.5,0,0.5)
        for strength in (0.5,1):
            actual=self.candidate(incoming,smooth,strength)
            scaled=self.candidate(incoming,tuple(x*2 for x in smooth),strength)
            for a,b in zip(actual,scaled): self.assertAlmostEqual(a,b,places=12)
        expected_x=0.382683432365
        self.assertAlmostEqual(self.candidate(incoming,smooth,0.5)[0],expected_x,places=10)
        # Negative control: omitting smooth-normal normalization changes both
        # the fixture result and its response to positive scaling.
        raw=normalize(lerp(normalize(incoming),smooth,0.5))
        raw_scaled=normalize(lerp(normalize(incoming),tuple(x*2 for x in smooth),0.5))
        self.assertGreater(abs(raw[0]-expected_x),0.05)
        self.assertGreater(abs(raw[0]-raw_scaled[0]),0.1)

    def test_scale_invariance_does_not_extend_across_magnitude_guard(self):
        incoming=(0,0,1)
        below=(0.005,0,0.005)
        above=tuple(x*2 for x in below)
        self.assertLess(sum(x*x for x in below),0.0001)
        self.assertGreater(sum(x*x for x in above),0.0001)
        self.assertEqual(self.candidate(incoming,below,0.5),incoming)
        self.assertGreater(self.candidate(incoming,above,0.5)[0],0.3)

    def test_zero_and_below_threshold_fields_preserve_incoming_normal(self):
        for smooth in ((0,0,0),(0.001,0,0)):
            for strength in (0,0.5,1,2):
                self.assertEqual(self.candidate((0,0,2),smooth,strength),(0,0,2))

    def test_full_strength_does_not_normalize_unused_zero_incoming_normal(self):
        self.assertEqual(self.candidate((0,0,0),(0,0,0.5),1),(0,0,1))

if __name__=='__main__': unittest.main()

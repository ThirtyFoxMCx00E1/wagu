import sys, unittest
from pathlib import Path
from types import SimpleNamespace as N
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from asset_catalog_dragon import clumps
class DragonApiTests(unittest.TestCase):
    def make(self):return N(frame_list=[],geometry_list=[],atomic_list=[])
    def test_legacy_single(self):
        c=self.make();self.assertEqual(clumps(c),[c])
    def test_new_multiple_keeps_local_frames(self):
        a,b=self.make(),self.make();self.assertEqual(clumps(N(clumps=[a,b])),[a,b])
    def test_invalid_api_fails(self):
        for d in [N(),N(clumps=[]),N(clumps=[N()])]:
            with self.assertRaises(ValueError):clumps(d)

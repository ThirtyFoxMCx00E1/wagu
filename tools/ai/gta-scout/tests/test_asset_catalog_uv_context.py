import math,sys,unittest
from pathlib import Path
from types import SimpleNamespace as N
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import asset_catalog_uv_context as u
class UVContextTests(unittest.TestCase):
    def fixture(self):
        return N(frame_list=[],atomic_list=[],geometry_list=[N(materials=[N(textures=[N(name='Selected')]),N(textures=[N(name='Other')])],triangles=[N(a=0,b=1,c=2,material=0),N(a=0,b=2,c=1,material=1)],uv_layers=[[N(u=0,v=0),N(u=2,v=-1),N(u=.5,v=1)]])])
    def test_exact_material_only_and_tiling_preserved(self):
        r=u.material_uvs(self.fixture(),'selected');self.assertEqual(len(r),1);self.assertEqual(r[0]['uv'][1],[2.,-1.]);self.assertEqual(r[0]['vertexIndices'],[0,1,2])
    def test_absent_material_rejected(self):
        with self.assertRaisesRegex(ValueError,'No UV triangles'):u.material_uvs(self.fixture(),'missing')
    def test_missing_or_invalid_uv_rejected(self):
        data=self.fixture();data.geometry_list[0].uv_layers=[]
        with self.assertRaisesRegex(ValueError,'no UV layer'):u.material_uvs(data,'Selected')
        data=self.fixture();data.geometry_list[0].uv_layers[0][0].u=math.nan
        with self.assertRaisesRegex(ValueError,'Non-finite'):u.material_uvs(data,'Selected')
if __name__=='__main__':unittest.main()

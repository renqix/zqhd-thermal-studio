"""公开工作台的无硬件测试。"""
import sys,unittest,tempfile
from unittest.mock import patch
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'web'))
import app
from enhancement import PreviewEnhancer,DEFAULTS,validate_options
class WebTests(unittest.TestCase):
    def test_origin_and_params(self):
        client=app.app.test_client();headers={'Host':'127.0.0.1:8766'}
        self.assertEqual(client.get('/api/state',headers=headers).status_code,200)
        self.assertEqual(client.get('/api/state',headers={'Host':'evil.test'}).status_code,403)
        self.assertEqual(client.post('/api/enhance',json={'method':'faithful'},headers=headers).status_code,403)
        headers['X-Thermal-Token']=app.TOKEN
        for data in [{'denoise':101},{'temporal':1},{'method':'bad'}]:self.assertEqual(client.post('/api/enhance',json=data,headers=headers).status_code,400)
        previous=app.ROOT
        try:
            with tempfile.TemporaryDirectory() as directory:
                app.ROOT=Path(directory)
                self.assertEqual(client.post('/api/enhance',json={'method':'faithful','detail':30},headers=headers).status_code,200)
                self.assertTrue((Path(directory)/'preview-settings.json').exists())
        finally:app.ROOT=previous
    def test_contour_and_background_bounds(self):
        client=app.app.test_client();headers={'Host':'127.0.0.1:8766','X-Thermal-Token':app.TOKEN}
        for data in [{'contour':-1},{'contour':2},{'contour':1.5},{'contour':True},{'background':-1},{'background':101},{'background':35.5},{'filter':2},{'interpolation':3}]:
            self.assertEqual(client.post('/api/params',json=data,headers=headers).status_code,400)
        with patch.object(app.bridge,'send',return_value=None) as send:
            for data in [{'contour':0},{'contour':1},{'background':0},{'background':35},{'background':100}]:
                self.assertEqual(client.post('/api/params',json=data,headers=headers).status_code,200)
            self.assertEqual(send.call_count,5)
    def test_flat_and_input_unchanged(self):
        processor=PreviewEnhancer('.',load_models=False);raw=np.full((32,32),25,np.float32);original=raw.copy()
        output=processor.render(raw,20,30,DEFAULTS,1,1.)
        self.assertLess(float(np.max(np.abs(output-.5))),1e-4);np.testing.assert_array_equal(raw,original)
if __name__=='__main__':unittest.main()

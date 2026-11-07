import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))
sys.path.insert(0, str(ROOT / 'tests'))
import model
import weather
import wire
import clock_pb2 as pb
from test_wire import nanopb_size, nanopb_array


class FaceWeatherTests(unittest.TestCase):
    def setUp(self):
        self.hashes = [f'{i:016x}' for i in range(5)]
        self.assets = {h: {'kind':'bg','w':320,'h':240,'bytes':20000} for h in self.hashes[:3]}
        self.assets[self.hashes[3]] = {'kind':'sprite','w':2,'h':14,'bytes':96}
        self.assets[self.hashes[4]] = {'kind':'sprite','w':22,'h':42,'bytes':2784}
        self.data = {'name':'Weather test','background':self.hashes[0],
                     'time':{'x':160,'y':30,'size':40,'color':'#221122','align':'center','shadow':''},
                     'layers':[], 'particles':[], 'weather':{
                         'cloudy':{'background':self.hashes[1],'layers':[],'particles':[],'text_color':''},
                         'rain':{'background':self.hashes[2],'layers':[], 'particles':[{
                             'kind':'fall','sprites':[self.hashes[3]],'count':28,'area':{'x':0,'y':0,'w':320,'h':240},
                             'speed_min':100,'speed_max':150,'sway':0,'period':4000,'phase':0,'depth':'front','seed':1}],
                             'text_color':'#fff4ee'}}}

    def test_variant_assets_are_retained_and_encoded(self):
        face, report = model.check_face(self.data, self.assets, 'a'*12)
        self.assertEqual(report['errors'], [])
        self.assertEqual(model.draft_hashes(self.data), set(self.hashes[:4]))
        self.assertEqual(set(face['assets']), set(self.hashes[:4]))
        msg = pb.Face.FromString(wire.face_message(dict(face,rev='b'*12)).SerializeToString())
        self.assertEqual(msg.rain.background.hash,self.hashes[2])
        self.assertEqual(msg.rain.emitters[0].count,28)
        self.assertEqual(msg.rain.text_color,0xfff4ee)
        self.assertEqual(nanopb_array('FaceWeatherVariant','layers'),model.MAX_LAYERS)
        self.assertEqual(nanopb_array('FaceWeatherVariant','emitters'),model.MAX_EMITTERS)
        self.assertLessEqual(len(msg.SerializeToString()),nanopb_size('Face'))

    def test_default_and_schedule_unchanged_and_rain_overrides_text(self):
        face = model.clean_face(self.data,self.assets,'a'*12)
        self.assertEqual(model.weather_scene(face,'default')['background'],self.hashes[0])
        self.assertEqual(model.weather_scene(face,'rain')['time']['color'],'#fff4ee')
        self.assertEqual(face['time']['color'],'#221122')
        self.assertEqual(model.weather_scene(face,'rain')['when'],face['when'])
        self.assertIsNone(model.weather_scene(face,'rain')['date'])

    def test_bad_variation_is_not_hidden_by_good_default(self):
        self.data['weather']['rain']['particles'][0]['area']['w']=321
        _, report = model.check_face(self.data,self.assets,'a'*12)
        self.assertTrue(any(e.startswith('rain:') and 'off the screen' in e for e in report['errors']))
        self.data['weather']['hail']={}
        with self.assertRaises(model.Invalid):model.clean_face(self.data,self.assets,'a'*12)

    def test_legacy_face_without_variants_remains_unchanged(self):
        self.data.pop('weather')
        face = model.clean_face(self.data,self.assets,'a'*12)
        self.assertNotIn('weather',face)
        self.assertFalse(wire.face_message(dict(face,rev='b'*12)).HasField('rain'))

    def test_random_lightning_period_is_validated_and_sent_to_firmware(self):
        layer={'name':'Lightning','sprite':self.hashes[4],'x':0,'y':0,'motion':[{'type':'blink','period':1000,'on':250}],
               'scatter':{'area':{'x':150,'y':40,'w':120,'h':100},'seed':719,'period':1000}}
        self.data['weather']['rain']['layers']=[layer]
        face,report=model.check_face(self.data,self.assets,'a'*12)
        self.assertEqual(report['errors'],[])
        msg=wire.face_message(dict(face,rev='b'*12))
        self.assertEqual(msg.rain.layers[0].scatter_period_ms,1000)
        layer['scatter']['period']=100
        with self.assertRaises(model.Invalid):model.clean_face(self.data,self.assets,'a'*12)

    def test_python_js_and_firmware_agree_for_every_code(self):
        codes=list(range(101))+[255,999]
        expected=[weather.face_weather(c) for c in codes]
        script="const w=require('./public/face-weather.js');console.log(JSON.stringify("+json.dumps(codes)+".map(w.group)))"
        self.assertEqual(json.loads(subprocess.check_output(['node','-e',script],cwd=ROOT)),expected)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'weather.cpp';binary=Path(tmp)/'weather'
            src.write_text('#include <cstdio>\n#include "face_weather.h"\nint main(){unsigned codes[]={' + ','.join(map(str,codes)) + '};for(auto c:codes)printf("%d\\n",int(faceweather::group(c)));}')
            subprocess.run(['c++','-std=c++17','-I',str(ROOT.parent/'firmware'/'BhoomiClock'),str(src),'-o',str(binary)],check=True)
            result=list(map(int,subprocess.check_output([str(binary)]).split()))
        self.assertEqual(result,[{'default':0,'cloudy':1,'rain':2}[e] for e in expected])
        self.assertEqual(weather.face_weather(97),'rain')
        for code in [0,1,71,73,75,77,85,86]:self.assertEqual(weather.face_weather(code),'default')

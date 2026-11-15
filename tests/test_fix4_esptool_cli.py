import importlib.util, pathlib, sys, unittest
from unittest.mock import patch
from types import SimpleNamespace
p=pathlib.Path(__file__).resolve().parents[1] / 'tools' / 'flash_detected.py'
sys.path.insert(0,str(p.parent))
spec=importlib.util.spec_from_file_location('vinyl_fix4',p)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def result(out='',rc=0): return SimpleNamespace(stdout=out,stderr='',returncode=rc)

class Test(unittest.TestCase):
 def test_v4_read_only(self):
  with patch.object(m.subprocess,'run',side_effect=[result('4.9.0\n'),result('Chip is ESP32-S3\nDetected flash size: 16MB\n')]) as mock:
   o=m.esptool('python','COM13','flash-id',before='default-reset')
  argv=mock.call_args_list[1].args[0]
  self.assertIn('flash_id',argv);self.assertIn('default_reset',argv)
  self.assertIn('no_reset',argv)
  self.assertNotIn('flash-id',argv)
  self.assertIn('Detected flash size',o)
 def test_v4_read_flash(self):
  with patch.object(m.subprocess,'run',side_effect=[result('4.9.0\n'),result('Read 0x1000')]) as mock:
   m.esptool('python','COM13','read-flash','0x8000','0x1000','file',before='no-reset')
  argv=mock.call_args_list[1].args[0]
  self.assertIn('read_flash',argv);self.assertIn('no_reset',argv)
 def test_v4_write(self):
  with patch.object(m.subprocess,'run',side_effect=[result('4.9.0\n'),result('PASS')]) as mock:
   m.esptool('python','COM13','write-flash','0x20000','file',after='hard-reset')
  argv=mock.call_args_list[1].args[0]
  self.assertIn('write_flash',argv);self.assertIn('hard_reset',argv)
 def test_v5_read(self):
  with patch.object(m.subprocess,'run',side_effect=[result('5.3.0\n'),result('SUCCESS')]) as mock:
   m.esptool('python','COM14','read-flash','0x8000','0x1000','file')
  argv=mock.call_args_list[1].args[0]
  self.assertIn('read-flash',argv);self.assertIn('no-reset',argv)
 def test_v5_write(self):
  with patch.object(m.subprocess,'run',side_effect=[result('5.3.0\n'),result('SUCCESS')]) as mock:
   m.esptool('python','COM14','write-flash','0x20000','firmware.bin',after='hard-reset')
  argv=mock.call_args_list[1].args[0]
  self.assertIn('write-flash',argv);self.assertIn('hard-reset',argv)
 def test_unknown_fails_without_device(self):
  with patch.object(m.subprocess,'run',return_value=result('6.0.0\n')) as mock:
   with self.assertRaises(m.VerifyError):m.esptool('python','COM13','flash-id',before='default-reset')
  self.assertEqual(mock.call_count,1)
  self.assertIn('-c',mock.call_args.args[0])
 def test_failure_stops(self):
  with patch.object(m.subprocess,'run',side_effect=[result('4.9.0\n'),result('port busy',rc=1)]):
   with self.assertRaises(m.VerifyError):m.esptool('python','COM13','flash-id',before='default-reset')

unittest.main(verbosity=2)

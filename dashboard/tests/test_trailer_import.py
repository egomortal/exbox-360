"""Native trailer stream framing and collision/preservation boundaries."""
from pathlib import Path
from io import BytesIO
import json,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from trailer_import import multipart_frames,title_for,import_one,inspect as inspect_video

class TrailerImportTests(unittest.TestCase):
 def test_binary_marker_inside_frame_is_not_split(self):
  frame=b'\xff\xd8payload--exboxframe\r\n\xff\xd9'
  stream=b'--exboxframe\r\nContent-Type: image/jpeg\r\nContent-Length: '+str(len(frame)).encode()+b'\r\n\r\n'+frame+b'\r\n--exboxframe--\r\n'
  self.assertEqual(list(multipart_frames(BytesIO(stream))),[frame])
 def test_ffmpeg_final_empty_boundary_is_accepted(self):
  self.assertEqual(list(multipart_frames(BytesIO(b'--exboxframe\r\n'))),[])
 def test_truncated_or_oversize_frame_refused(self):
  for size,payload in [(20,b'no'),(10000001,b''),(0,b'')]:
   stream=b'--exboxframe\r\nContent-Length: '+str(size).encode()+b'\r\n\r\n'+payload
   with self.assertRaises(ValueError):list(multipart_frames(BytesIO(stream)))
 def test_title_cannot_inject_manifest_row(self):
  with tempfile.TemporaryDirectory() as temp:
   source=Path(temp)/'test.mp4';source.with_suffix('.mp4.json').write_text(json.dumps(dict(title='bad\nrow')))
   with self.assertRaises(ValueError):title_for(source)
 def test_default_title_and_unicode_sidecar(self):
  with tempfile.TemporaryDirectory() as temp:
   source=Path(temp)/'My clip.mp4';self.assertEqual(title_for(source),'My clip')
   source.with_suffix('.mp4.json').write_text(json.dumps(dict(title='My trailer — preview')))
   self.assertEqual(title_for(source),'My trailer — preview')
 def test_portrait_metadata_preserves_display_dimensions(self):
  with tempfile.TemporaryDirectory() as temp:
   source=Path(temp)/'video.mov';source.write_bytes(b'fixture')
   stream=dict(index=0,codec_type='video',width=320,height=180,r_frame_rate='24/1',display_aspect_ratio='16:9',side_data_list=[dict(rotation=90)])
   with patch('trailer_import.subprocess.check_output',return_value=json.dumps(dict(streams=[stream],format=dict(duration='1')))):
    profile,audio=inspect_video(source)
   self.assertEqual((profile['width'],profile['height'],profile['aspect_num'],profile['aspect_den']),(180,320,9,16));self.assertFalse(audio)
 def test_hdr_refused_before_color_conversion(self):
  with tempfile.TemporaryDirectory() as temp:
   source=Path(temp)/'video.mp4';source.write_bytes(b'fixture')
   stream=dict(index=0,codec_type='video',color_transfer='smpte2084')
   with patch('trailer_import.subprocess.check_output',return_value=json.dumps(dict(streams=[stream]))):
    with self.assertRaisesRegex(ValueError,'HDR'):inspect_video(source)
 def test_outside_private_output_rejected_before_encode(self):
  with tempfile.TemporaryDirectory() as temp:
   source=Path(temp)/'test.mp4';source.write_bytes(b'not video')
   with patch('trailer_import.inspect') as probe:
    with self.assertRaises(ValueError):import_one(source,Path(temp)/'out')
    probe.assert_not_called()

if __name__=='__main__':unittest.main()

from pathlib import Path
import socketserver,threading,tempfile,subprocess,json,time
r=Path(__file__).resolve().parents[1];out=r/'private/native-dashboard/feed-tests';binary=out/'client-test'
subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(r/'modules/system_data/client_test.cpp'),str(r/'ps5_library.cpp'),'-o',str(binary)],check=True)
def entry(name,kind='f',size=1):return f'{kind}\t{size}\t123\t9\t{name.encode().hex()}\n'.encode()
files={'/user/appmeta/PPSA00001/param.json':b'{"titleName":"Test Game"}','/user/appmeta/PPSA00001/icon0.png':b'original-icon-bytes','/system_data/priv/home/user_list.dat':b'USER-LIST'}
counts={}
class Handler(socketserver.StreamRequestHandler):
 def handle(self):
  command=self.rfile.readline(9000).decode().rstrip('\n').split('\t');assert len(command)==4 and command[0]=='FEED1'
  _,verb,encoded,limit=command;path=bytes.fromhex(encoded).decode();limit=int(limit);counts[verb,path]=counts.get((verb,path),0)+1
  if path.endswith('badlen'):self.wfile.write(b'OK\t999999999999999999999999\n');return
  if path.endswith('short'):self.wfile.write(b'OK\t5\nxx');return
  if verb=='version':body=b'EXBOX-FEED 1\n'
  elif verb=='apps':body=('PPSA00001'.encode().hex()+'\t'+'Test Game'.encode().hex()+'\t'+'gd'.encode().hex()+'\t19\t123\n').encode()
  elif verb=='list':body=entry('PPSA00001','d') if path=='/user/appmeta' else entry('20261029_100000.jpg','f',7) if path=='/user/av_contents/photo' else b''
  elif verb=='file' and path in files:body=files[path][:limit]
  elif verb=='stat' and (path in files or path.endswith('.jpg')):body=entry(path,'f',len(files.get(path,b'picture')))
  else:self.wfile.write(b'ERR\t2\n');return
  data=f'OK\t{len(body)}\n'.encode()+body
  for start in range(0,len(data),7):self.wfile.write(data[start:start+7]);self.wfile.flush()
class Server(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
with Server(('127.0.0.1',0),Handler) as server,tempfile.TemporaryDirectory(prefix='exbox-feed-client-') as cache:
 thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 try:subprocess.run([str(binary),str(server.server_address[1]),cache],check=True,timeout=20)
 finally:server.shutdown();thread.join()
assert counts['file','/user/appmeta/PPSA00001/icon0.png']==1
assert counts['apps','']==1
assert counts.get(('file','/user/appmeta/PPSA00001/param.json'),0)==0
record={'split_responses':'passed','game_names_icons':'passed','profile_prefix_read':'passed','capture_enumeration':'passed','original_byte_cache_and_reuse':'passed','overflow_and_short_body':'rejected','data_stays_direct':'passed'}
(out/'client-test.json').write_text(json.dumps(record,indent=2)+'\n');print(record)

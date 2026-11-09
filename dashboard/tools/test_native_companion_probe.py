from pathlib import Path
import tempfile,subprocess,socket,time,os,json
root=Path(__file__).resolve().parents[1];out=root/'private/native-dashboard';binary=out/'companion-probe-test'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(root/'modules/native_dashboard/companion_probe_test_host.c'),'-o',str(binary)],check=True)
with tempfile.TemporaryDirectory(prefix='exbox-transport-') as temp:
 p=Path(temp);sock=p/'companion.sock';spool=p/'spool';proc=subprocess.Popen([str(binary),str(sock),str(spool)])
 try:
  for _ in range(100):
   if sock.exists():break
   time.sleep(.02)
  with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as client:
   client.settimeout(2);client.connect(str(sock));client.sendall(b'GET / HTTP/1.0\r\n\r\n');data=client.recv(1024);assert data.endswith(b'EXBOX-PROBE-1 OK\n') and b'Content-Length: 17' in data
  (spool/'probe-ABC123.req').write_bytes(b'EXBOX-PROBE-1\n')
  (spool/'probe-BAD123.req').write_bytes(b'EXBOX-PROBE-1\nmalformed')
  target=p/'outside';target.write_bytes(b'EXBOX-PROBE-1\n');(spool/'probe-SYM123.req').symlink_to(target)
  for _ in range(100):
   if (spool/'probe-ABC123.rsp').exists():break
   time.sleep(.02)
  assert (spool/'probe-ABC123.rsp').read_bytes()==b'EXBOX-PROBE-1 OK\n'
  assert not (spool/'probe-ABC123.req').exists()
  time.sleep(.1)
  assert not (spool/'probe-BAD123.rsp').exists() and not (spool/'probe-SYM123.rsp').exists()
  assert target.read_bytes()==b'EXBOX-PROBE-1\n'
  assert proc.wait(timeout=5)==0 and not sock.exists()
 finally:
  if proc.poll() is None:proc.terminate();proc.wait()
result={'af_unix_ping':'passed','atomic_spool_ping':'passed','malformed_request':'rejected','symlink_request':'rejected','console_tested':False}
(out/'q8d-transport-host-test.json').write_text(json.dumps(result,indent=2)+'\n');print(result)

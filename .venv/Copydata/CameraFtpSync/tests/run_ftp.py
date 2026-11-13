"""Runs only against a loopback FTP server in an isolated disposable directory."""
import pathlib, tempfile, threading, subprocess, sys, logging
from pyftpdlib.log import config_logging
config_logging(level=logging.ERROR)
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer
exe=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='camera-ftp-') as tmp:
    root=pathlib.Path(tmp); (root/'server').mkdir(); (root/'source').mkdir()
    auth=DummyAuthorizer(); auth.add_user('camera','integration-only',str(root/'server'),perm='elradfmwMT')
    class Handler(FTPHandler):
        def on_login(self, username): pass
    Handler.authorizer=auth
    server=FTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,kwargs={'timeout':0.1},daemon=True);thread.start()
    port=server.socket.getsockname()[1]
    try:
        for password,mode in [('wrong',['auth']),('integration-only',[])]:
            result=subprocess.run([str(exe),str(root/'source'),str(port),password,*mode],timeout=120)
            if result.returncode: raise SystemExit(result.returncode)
        actual=(root/'server'/'PC01'/'lsc.txt').read_bytes()
        assert actual.replace(b'\r\n',b'\n')==b'first\nsecond\n',repr(actual)
        assert sorted(p.relative_to(root/'server'/'PC01').as_posix() for p in (root/'server'/'PC01').rglob('*') if p.is_file())==['lsc.txt','sub/nested.txt']
        print('PASS integration server final bytes and no leftover rollback/upload/probe files')
    finally:
        server.close_all();thread.join(timeout=2)

import os
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer
root=os.environ['DEMO_FTP_ROOT']
auth=DummyAuthorizer(); auth.add_user('demo','DemoOnly_123!',root,perm='elradfmwMT')
class Handler(FTPHandler): pass
Handler.authorizer=auth
server=FTPServer(('127.0.0.1',2121),Handler)
print('Listening on 127.0.0.1:2121; root='+root,flush=True)
server.serve_forever()
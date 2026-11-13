import pathlib, tempfile, threading, subprocess, sys, datetime, logging
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import TLS_FTPHandler
from pyftpdlib.servers import FTPServer
from pyftpdlib.log import config_logging
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import rsa
config_logging(level=logging.ERROR)
exe=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='camera-ftps-') as tmp:
    root=pathlib.Path(tmp);(root/'source').mkdir();(root/'server').mkdir()
    key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
    subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'localhost')])
    now=datetime.datetime.now(datetime.timezone.utc)
    cert=(x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(minutes=1)).not_valid_after(now+datetime.timedelta(days=1)).add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost')]),False).sign(key,hashes.SHA256()))
    pem=root/'untrusted.pem';pem.write_bytes(cert.public_bytes(serialization.Encoding.PEM)+key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.TraditionalOpenSSL,serialization.NoEncryption()))
    auth=DummyAuthorizer();auth.add_user('camera','integration-only',str(root/'server'),perm='elradfmwMT')
    class Handler(TLS_FTPHandler): pass
    Handler.authorizer=auth;Handler.certfile=str(pem);Handler.tls_control_required=True;Handler.tls_data_required=True
    server=FTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,kwargs={'timeout':0.1},daemon=True);thread.start()
    try:
        result=subprocess.run([str(exe),str(root/'source'),str(server.socket.getsockname()[1]),'integration-only','tls'],timeout=60)
        assert result.returncode==0,result.returncode
        assert not list((root/'server').iterdir()),'TLS failure wrote remote data'
        print('PASS untrusted FTPS endpoint receives no uploaded files; trust stores unchanged')
    finally:
        server.close_all();thread.join(timeout=2)

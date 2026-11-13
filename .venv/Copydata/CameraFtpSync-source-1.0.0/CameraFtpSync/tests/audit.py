"""Audits the source/runtime inventory; documentation may name excluded technologies."""
import pathlib, hashlib, tarfile, re, sys
root=pathlib.Path(__file__).resolve().parents[1]
for line in (root/'third_party/SHA256SUMS.txt').read_text().splitlines():
    digest,name=line.split('  ',1)
    assert hashlib.sha256((root/'third_party'/name).read_bytes()).hexdigest()==digest,name
with tarfile.open(root/'third_party/curl-8.12.1.tar.xz') as tar:
    assert not any(re.search(r'(sqlite3?\.(c|h|dll)|sqlite.*\.lib)$',n,re.I) for n in tar.getnames())
for folder in ['src','tests']:
    for p in (root/folder).rglob('*'):
        if p.suffix not in ['.cpp','.h']:continue
        text=p.read_text(encoding='utf-8-sig')
        assert not re.search(r'\bsqlite3_\w+|SQLDriverConnect|Driver=\{|sqlite3\.h',text,re.I),p
if len(sys.argv)>1:
    package=pathlib.Path(sys.argv[1])
    for p in package.rglob('*'):
        assert not re.search(r'(sqlite|sqlserver|odbc).*(dll|lib|exe)$',p.name,re.I),p
print('PASS source/dependency hashes and no database source/API/runtime dependency in audited inventory')

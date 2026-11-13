"""Create a source-only ZIP; intentionally excludes build outputs and local development tools."""
from pathlib import Path
import hashlib,zipfile
root=Path(__file__).resolve().parent
out=root.parent/'CameraFtpSync-source-1.0.0.zip'
allowed={'src','tests','docs','third_party'}
rootfiles={'README.md','CMakeLists.txt','CMakePresets.json','CameraFtpSync.sln','CameraFtpSync.vcxproj','build.ps1','.clang-format','.gitignore','package-source.py'}
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as zip:
    for path in sorted(root.rglob('*')):
        relative=path.relative_to(root)
        if not path.is_file() or not (relative.parts[0] in allowed or str(relative) in rootfiles):continue
        if any(part=='__pycache__' for part in relative.parts) or path.suffix in {'.exe','.dll','.o','.obj','.pdb','.pyc','.a','.lib'}:continue
        zip.write(path,'CameraFtpSync/'+relative.as_posix())
    specification=root.parent/'CODEX_CAMERA_FTP_SYNC_NO_SQLITE.md'
    if specification.is_file():zip.write(specification,'CameraFtpSync/'+specification.name)
digest=hashlib.sha256(out.read_bytes()).hexdigest()
out.with_suffix('.zip.sha256').write_text(digest+'  '+out.name+'\n')
print(f'Source package: {out.name}, {out.stat().st_size} bytes, SHA256 {digest}')

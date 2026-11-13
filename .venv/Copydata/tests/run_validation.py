"""Execute built test programs and retain unedited evidence with explicit skipped subchecks."""
import pathlib, subprocess, sys, json, datetime, os
root=pathlib.Path(__file__).resolve().parents[1]
build=pathlib.Path(sys.argv[1]).resolve()
output=root/'docs'/'test-results';output.mkdir(parents=True,exist_ok=True)
commands=[('core',[str(build/'CameraCoreTests.exe')]),('resources',[str(build/'CameraResourceTests.exe')]),('ftp',[sys.executable,str(root/'tests/run_ftp.py'),str(build/'CameraFtpIntegration.exe')]),('audit',[sys.executable,str(root/'tests/audit.py')])]
if '--tls' in sys.argv:commands.append(('ftps',[sys.executable,str(root/'tests/run_ftps.py'),str(build/'CameraFtpIntegration.exe')]))
summary=[]
for name,command in commands:
    result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    text=result.stdout.decode('utf-8',errors='replace')
    (output/(name+'.txt')).write_text(text,encoding='utf-8')
    skips=[line for line in text.splitlines() if line.startswith('SKIP ')]
    summary.append({'suite':name,'exit_code':result.returncode,'skipped_subchecks':skips})
    print(name,'exit',result.returncode,*skips,sep=' | ')
    if result.returncode:print(text)
(output/'summary.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'build_directory':str(build),'compiler_cache_evidence':[line for line in (build/'CMakeCache.txt').read_text().splitlines() if line.startswith(('CMAKE_CXX_COMPILER:','CMAKE_BUILD_TYPE:'))],'results':summary},indent=2),encoding='utf-8')
sys.exit(1 if any(r['exit_code'] for r in summary) else 0)

"""Validate a candidate in a separate SDK-free runtime directory on the developer host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile
import psutil

p=argparse.ArgumentParser()
p.add_argument('--package',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--build',type=Path,default=Path('build/windows'))
a=p.parse_args()
package=a.package.resolve(); out=a.out.resolve(); build=a.build.resolve()
if out.is_relative_to(package) or package.is_relative_to(out):
    raise ValueError('Validation output and candidate directory must not overlap')
manifest=json.loads((package/'manifest.json').read_text(encoding='utf-8'))
for name,item in manifest['files'].items():
    relative=Path(name)
    if relative.drive or relative.is_absolute() or '..' in relative.parts:
        raise ValueError('Invalid candidate manifest path')
    file=(package/relative).resolve()
    if not file.is_relative_to(package): raise ValueError('Candidate file escapes package')
    if file.stat().st_size!=item['bytes'] or hashlib.sha256(file.read_bytes()).hexdigest()!=item['sha256']:
        raise RuntimeError('Package hash mismatch: '+name)
with zipfile.ZipFile(package/'source.zip') as source:
    for name,expected in manifest['source_sha256'].items():
        if hashlib.sha256(source.read(name)).hexdigest()!=expected:
            raise RuntimeError('Source archive mismatch: '+name)
out.mkdir(parents=True,exist_ok=False)
runtime=out/'runtime'
shutil.copytree(package,runtime)
env=os.environ.copy()
for key in list(env):
    if key.startswith(('QT_', 'QML', 'QSG_', 'LANGLERN_')): env.pop(key)
env['PATH']=str(Path(os.environ['SystemRoot'])/'System32')+os.pathsep+os.environ['SystemRoot']
env['QT_FORCE_STDERR_LOGGING']='1'
env['QT_MESSAGE_PATTERN']='%{message}'
env['LANGLERN_WORKSPACE_DIR']=str(out/'workspaces')
records=[]
# Probe the shipped executable before introducing the test-only DLL.
process=subprocess.Popen([str(runtime/'langlern.exe'),'--startup-probe','--quit-after-ready'],
                         cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
modules=set()
deadline=time.monotonic()+20
while process.poll() is None:
    if time.monotonic()>deadline:
        process.kill(); process.wait()
        raise TimeoutError('Shipped product startup exceeded 20 seconds')
    try: modules.update(m.path for m in psutil.Process(process.pid).memory_maps())
    except (psutil.NoSuchProcess,psutil.AccessDenied): pass
    time.sleep(.005)
stdout=process.communicate(timeout=20)[0]
(out/'product-startup.log').write_bytes(stdout)
if process.returncode or b'interactive_proxy' not in stdout: raise RuntimeError('Product startup failed')
audit=json.loads((package/'licenses/deployed-binaries.json').read_text(encoding='utf-8'))
qt_names={Path(item['file']).name.lower() for item in audit if item.get('sbom')}
qt_modules=[s for s in modules if Path(s).name.lower().startswith('qt6') or Path(s).name.lower() in qt_names]
if not qt_modules or any(not Path(s).is_relative_to(runtime) for s in qt_modules):
    raise RuntimeError('Qt module loaded outside isolated candidate: '+str(qt_modules))
records.append({'test':'product_startup','exit':process.returncode,'qt_modules':sorted(qt_modules)})
root=Path(__file__).resolve().parents[1]
shutil.copy2(root/'.deps/Qt/6.8.3/msvc2022_64/bin/Qt6Test.dll',runtime)
for name in ['shell_tests','reading_tests','text_tests','annotation_tests','ink_tests','storage_tests','design_tests']:
    shutil.copy2(build/(name+'.exe'),runtime)
    result=subprocess.run([str(runtime/(name+'.exe')),'-o',str(out/(name+'.log'))+',txt'],cwd=out,env=env,timeout=180)
    records.append({'test':name,'exit':result.returncode})
    if result.returncode: raise RuntimeError(name+' failed; see retained logs')
(out/'summary.json').write_text(json.dumps({'hashes_verified':len(manifest['files']),
    'path':env['PATH'],'scope':'SDK-free PATH on development host; not a clean VM', 'tests':records},indent=2),encoding='utf-8')
print('PASS: manifest, product startup/module provenance and 7 isolated runtime suites')

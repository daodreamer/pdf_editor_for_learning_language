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
process=subprocess.Popen([str(runtime/'LinguaPDF.exe'),'--startup-probe','--quit-after-ready'],
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
# Explorer, "Open with" and pinned shortcuts read the exe icon resource; the title bar,
# taskbar button and Alt+Tab use the window icon set at startup. Both must be present.
import ctypes
exe_icons=ctypes.windll.shell32.ExtractIconExW(str(runtime/'LinguaPDF.exe'),-1,None,None,0)
window_icons=next((json.loads(line).get('window_icon_sizes',0) for line in stdout.decode('utf-8','replace').splitlines()
                   if '"application_ready"' in line),0)
if exe_icons<1 or window_icons!=7:
    raise RuntimeError(f'Application icon missing: exe resource icons={exe_icons}, window icon sizes={window_icons}/7')
audit=json.loads((package/'licenses/deployed-binaries.json').read_text(encoding='utf-8'))
qt_names={Path(item['file']).name.lower() for item in audit if item.get('sbom')}
qt_modules=[s for s in modules if Path(s).name.lower().startswith('qt6') or Path(s).name.lower() in qt_names]
if not qt_modules or any(not Path(s).is_relative_to(runtime) for s in qt_modules):
    raise RuntimeError('Qt module loaded outside isolated candidate: '+str(qt_modules))
records.append({'test':'product_startup','exit':process.returncode,'qt_modules':sorted(qt_modules),
                'exe_icons':exe_icons,'window_icon_sizes':window_icons})
root=Path(__file__).resolve().parents[1]
shutil.copy2(root/'.deps/Qt/6.8.3/msvc2022_64/bin/Qt6Test.dll',runtime)
shutil.copy2(build/'langlern_tests.exe',runtime)
# Suites that load each shipped runtime component: Qt Quick/QML, PDFium render/export,
# SQLite dictionary plugin, Tesseract OCR and the natural speech engine. The remaining
# suites exercise the same modules and already run under CTest against the build tree.
suites=['shell','storage','design','ocr','speech']
for name in suites:
    result=subprocess.run([str(runtime/'langlern_tests.exe'),name,'-o',str(out/(name+'.log'))+',txt'],cwd=out,env=env,timeout=180)
    records.append({'test':name,'exit':result.returncode})
    if result.returncode: raise RuntimeError(name+' failed; see retained logs')
(out/'summary.json').write_text(json.dumps({'hashes_verified':len(manifest['files']),
    'path':env['PATH'],'scope':'SDK-free PATH on development host; not a clean VM', 'tests':records},indent=2),encoding='utf-8')
print(f'PASS: manifest, product startup/module provenance and {len(suites)} isolated runtime suites')

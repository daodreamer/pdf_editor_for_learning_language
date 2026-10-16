"""Create a local unsigned Windows candidate. Never overwrite an existing artifact."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import zipfile
from build_provenance import product_inputs

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRECTORIES=('src','qml','assets','tools','tests','docs','third_party','.github','prototypes','benchmarks')

def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def unsigned_vendor_match(data, expected):
    """Vendor SBOM hashes can precede Authenticode signing; compare without changing the DLL."""
    try:
        pe=struct.unpack_from('<I',data,0x3c)[0];optional=pe+24
        if data[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',data,optional)[0]!=0x20b: return None
        entry=optional+112+8*4
        offset,size=struct.unpack_from('<II',data,entry)
        if not offset or not size or offset+size!=len(data): return None
        bare=bytearray(data[:offset]);bare[entry:entry+8]=b'\0'*8;bare[optional+64:optional+68]=b'\0'*4
        return hashlib.sha1(bare).hexdigest()==expected
    except (struct.error,IndexError):
        return None

def package(build, out, vs):
    build, out = build.resolve(), out.resolve()
    if any(out.is_relative_to(ROOT/directory) for directory in SOURCE_DIRECTORIES):
        raise ValueError('Candidate output must not be inside a source directory')
    qt = ROOT / '.deps/Qt/6.8.3/msvc2022_64'
    provenance=json.loads((build/'product-build.json').read_text(encoding='utf-8'))
    if provenance['source_sha256']!=product_inputs() or provenance['exe_sha256']!=sha(build/'langlern.exe'):
        raise RuntimeError('Product/source changed since build; run build-windows.ps1 again')
    configuration=provenance['configuration']
    flags=' '.join(value for key,value in configuration.items() if key!='CMAKE_BUILD_TYPE').lower()
    if configuration.get('CMAKE_BUILD_TYPE')!='Release' or 'sanitize' in flags:
        raise RuntimeError('Candidate packaging requires a regular Release build')
    for p in [build/'langlern.exe', build/'pdfium.dll', build/'fonts/NotoSansSC-Regular.ttf']:
        if not p.is_file():
            raise FileNotFoundError(p)
    if sha(build/'pdfium.dll')!=sha(ROOT/'.deps/pdfium-win/bin/pdfium.dll'):
        raise RuntimeError('PDFium build copy differs from the locked dependency directory')
    if sha(build/'fonts/NotoSansSC-Regular.ttf')!=sha(ROOT/'assets/fonts/NotoSansSC-Regular.ttf'):
        raise RuntimeError('Packaged font differs from current source; rebuild first')
    archive = out.parent/(out.name+'.zip')
    checksum = out.parent/(out.name+'.sha256')
    if archive.exists() or checksum.exists():
        raise FileExistsError('Archive/checksum already exists')
    out.mkdir(parents=True, exist_ok=False)
    shutil.copy2(build/'langlern.exe', out)
    shutil.copy2(build/'pdfium.dll', out)
    shutil.copytree(build/'fonts', out/'fonts')
    if (build/'dictionary').is_dir():
        shutil.copytree(build/'dictionary', out/'dictionary')
    command = [str(qt/'bin/windeployqt.exe'), '--release', '--qmldir', str(ROOT/'qml'),
               '--no-compiler-runtime', '--no-translations', '--no-opengl-sw',
               '--no-system-d3d-compiler', '--no-system-dxc-compiler',
               '--skip-plugin-types', 'qmltooling,generic,networkinformation,tls,sqldrivers',
               '--verbose', '1', str(out/'langlern.exe')]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out/'deployment.log').write_bytes(result.stdout)
    result.check_returncode()
    (out/'sqldrivers').mkdir(exist_ok=True)
    shutil.copy2(qt/'plugins/sqldrivers/qsqlite.dll',out/'sqldrivers/qsqlite.dll')
    sdk=Path(os.environ['WindowsSdkDir'])
    sdk_version=os.environ['WindowsSDKVersion'].strip('\\/')
    d3d_compiler=sdk/'Redist/D3D/x64/d3dcompiler_47.dll'
    shutil.copy2(d3d_compiler,out)
    # Use only the toolchain's redistributable directory, never system32 or compiler binaries.
    redist_root = vs/'VC/Redist/MSVC'
    version_file = vs/'VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt'
    runtime = redist_root/version_file.read_text().strip()/'x64/Microsoft.VC145.CRT'
    if not runtime.is_dir():
        # VS 2022 CI uses VC143. Choose the installed toolchain's own latest redist.
        candidates = list(redist_root.glob('*/x64/Microsoft.VC*.CRT'))
        if not candidates: raise FileNotFoundError('MSVC x64 redistributable CRT')
        runtime = max(candidates, key=lambda p: tuple(map(int, p.parents[1].name.split('.'))))
    for dll in runtime.glob('*.dll'):
        shutil.copy2(dll, out)
    (out/'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\n', encoding='utf-8')
    licenses = out/'licenses'
    shutil.copytree(ROOT/'third_party/notices', licenses)
    shutil.copy2(build/'product-build.json',licenses)
    shutil.copy2(ROOT/'LICENSE', out)
    shutil.copy2(ROOT/'third_party/qt-sbom-evidence.json', licenses)
    shutil.copy2(ROOT/'third_party/dependencies.lock.json', licenses)
    shutil.copy2(ROOT/'third_party/dictionary.lock.json', licenses)
    shutil.copy2(vs/'Licenses/1033/Redist.txt', licenses/'Microsoft-Redist.txt')
    sdk_notices=licenses/'windows-sdk';sdk_notices.mkdir(exist_ok=True)
    for name in ('sdk_license.rtf','sdk_third_party_notices.rtf'):
        shutil.copy2(sdk/'Licenses'/sdk_version/name,sdk_notices/name)
    sboms = []
    notices = []
    vendor_files = {}
    for source in sorted((qt/'sbom').glob('*.spdx.json')):
        data = json.loads(source.read_text(encoding='utf-8'))
        sboms.append({'file': source.name, 'sha256': sha(source)})
        shutil.copy2(source, licenses)
        for item in data.get('files', []):
            vendor_files[item['fileName'].removeprefix('./').lower()] = (source.name,item)
        for pkg in data.get('packages', []):
            notices.append('\n'.join(str(pkg.get(k, '')) for k in ('name', 'versionInfo', 'copyrightText', 'licenseDeclared', 'attributionTexts')))
        for info in data.get('hasExtractedLicensingInfos', []):
            notices.append(info.get('licenseId', '')+'\n'+info.get('extractedText', ''))
    (licenses/'Qt-ThirdParty-Notices.txt').write_text('\n\n'.join(notices), encoding='utf-8')
    binary_audit = []
    for dll in sorted(out.rglob('*.dll')):
        relative=dll.relative_to(out).as_posix()
        candidates=[relative, 'bin/'+relative, 'plugins/'+relative]
        found=next((vendor_files[c.lower()] for c in candidates if c.lower() in vendor_files),None)
        record={'file':relative,'sha256':sha(dll)}
        if found:
            sbom,item=found
            record.update({'sbom':sbom,'spdx_id':item['SPDXID'],
                           'license':item.get('licenseConcluded','NOASSERTION'),
                           'copyright':item.get('copyrightText','NOASSERTION')})
            expected=next((c['checksumValue'] for c in item.get('checksums',[]) if c['algorithm']=='SHA1'),None)
            record['vendor_sha1_matches']=hashlib.sha1(dll.read_bytes()).hexdigest()==expected if expected else None
            if expected and not record['vendor_sha1_matches']:
                record['vendor_unsigned_sha1_matches']=unsigned_vendor_match(dll.read_bytes(),expected)
            source=qt/item['fileName'].removeprefix('./')
            record['installed_sdk_sha256_matches']=source.is_file() and sha(source)==sha(dll)
        elif dll.name.lower()=='pdfium.dll':
            record.update({'component':'PDFium','evidence':'pdfium/ notices and dependencies.lock.json'})
        elif dll.name.lower()=='d3dcompiler_47.dll':
            record.update({'component':'Windows SDK D3D11 compiler','evidence':'windows-sdk/sdk_license.rtf',
                           'redist_sha256_matches':sha(dll)==sha(d3d_compiler)})
        elif (runtime/dll.name).is_file():
            record.update({'component':'MSVC CRT','evidence':'Microsoft-Redist.txt',
                           'redist_sha256_matches':sha(dll)==sha(runtime/dll.name)})
        else:
            record['evidence']='Unmapped binary: requires review before public release'
        binary_audit.append(record)
    (licenses/'deployed-binaries.json').write_text(json.dumps(binary_audit,ensure_ascii=False,indent=2),encoding='utf-8')
    for name in ['DISTRIBUTION.md', 'DEPENDENCIES.md']:
        shutil.copy2(ROOT/'third_party'/name, licenses/name)
    # Include actual working source used for this candidate, including uncommitted changes.
    source_paths = [ROOT/n for n in ['CMakeLists.txt','README.md','LICENSE','AGENTS.md','CONTRIBUTING.md']]
    for directory in SOURCE_DIRECTORIES:
        source_paths.extend(p for p in (ROOT/directory).rglob('*') if p.is_file()
                            and '__pycache__' not in p.parts and 'results' not in p.parts)
    source_hashes = {p.relative_to(ROOT).as_posix():sha(p) for p in sorted(source_paths)}
    with zipfile.ZipFile(out/'source.zip', 'x', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(source_paths): z.write(p,p.relative_to(ROOT).as_posix())
    shutil.copy2(ROOT/'docs/WINDOWS_PACKAGE.md', out/'README.md')
    inventory = {p.relative_to(out).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)}
                 for p in sorted(out.rglob('*')) if p.is_file()}
    manifest = {'candidate':'0.1.0-windows-x64', 'signed':False, 'qt':'6.8.3', 'pdfium':'157.0.8100.0',
                'windows_sdk':{'version':sdk_version,'d3dcompiler_source':'Redist/D3D/x64/d3dcompiler_47.dll',
                               'd3dcompiler_sha256':sha(d3d_compiler),'dxc_deployed':False},
                'runtime_source':str(runtime.relative_to(vs)), 'qt_sboms':sboms,
                'source_sha256':source_hashes, 'files':inventory,
                'scope':'Local candidate; clean VM, physical IME/input, cold startup and remote CI not verified. See licenses/DISTRIBUTION.md.'}
    (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file(): z.write(p, out.name+'/'+p.relative_to(out).as_posix())
    archive_sha=sha(archive)
    with checksum.open('x',encoding='ascii') as f:
        f.write(archive_sha+'  '+archive.name+'\n')
    print(json.dumps({'directory':str(out),'archive':str(archive),'bytes':archive.stat().st_size,
                      'sha256':archive_sha,'file_count':len(inventory)+1},indent=2))

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path,default=ROOT/'build/windows')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--vs',type=Path,required=True)
    args=parser.parse_args()
    package(args.build,args.out,args.vs)

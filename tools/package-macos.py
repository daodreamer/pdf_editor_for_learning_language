"""Build a self-contained LinguaPDF.app for local use on this Mac (Apple Silicon).

Usage (after configuring/building build/macos, see docs/BUILDING.md):
    .tools/venv/bin/python tools/package-macos.py [--build-dir build/macos] [--output-dir build/packages]

Steps: copy the built bundle, add runtime data and notices to Contents/Resources, bundle Qt with
macdeployqt, drop build-tree RPATHs, ad-hoc sign, then verify that a startup probe of the copied app
loads Qt/PDFium only from inside the bundle. The result is ad-hoc signed and NOT notarized: it runs on
the Mac that built it; distributing it to other Macs needs a Developer ID signature and notarization.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
QT = ROOT / '.deps/Qt/6.8.3/macos'


def run(*command, **options):
    return subprocess.run([str(part) for part in command], check=True, **options)


def output(*command):
    return subprocess.run([str(part) for part in command], check=True, capture_output=True, text=True).stdout


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def macho_files(bundle):
    for path in bundle.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
                    yield path


def rpaths(binary):
    lines = output('otool', '-l', binary).splitlines()
    return [lines[i + 2].split()[1] for i, line in enumerate(lines) if line.strip() == 'cmd LC_RPATH']


def copy_runtime_data(build, resources):
    for name in ('fonts', 'tessdata', 'dictionary', 'voices'):
        source = build / name
        if not source.is_dir():
            raise FileNotFoundError(f'{source} is missing; run the bootstrap scripts and rebuild first')
        shutil.copytree(source, resources / name, symlinks=True)
    for name in ('LICENSE', 'THIRD-PARTY-NOTICES.md', 'CHANGELOG.md'):
        shutil.copy2(ROOT / name, resources)
    licenses = resources / 'licenses'
    shutil.copytree(ROOT / 'third_party/notices', licenses)
    for name in ('dependencies.lock.json', 'dictionary.lock.json', 'dictionary-fr.lock.json', 'ocr.lock.json',
                 'speech.lock.json', 'qt-sbom-evidence.json'):
        shutil.copy2(ROOT / 'third_party' / name, licenses)
    notices = []
    for source in sorted((QT / 'sbom').glob('*.spdx.json')):
        shutil.copy2(source, licenses)
        data = json.loads(source.read_text(encoding='utf-8'))
        for package in data.get('packages', []):
            notices.append('\n'.join(str(package.get(k, '')) for k in
                                     ('name', 'versionInfo', 'copyrightText', 'licenseDeclared', 'attributionTexts')))
        for info in data.get('hasExtractedLicensingInfos', []):
            notices.append(info.get('licenseId', '') + '\n' + info.get('extractedText', ''))
    (licenses / 'Qt-ThirdParty-Notices.txt').write_text('\n\n'.join(notices), encoding='utf-8')


def verify(app):
    """Launch the packaged app and record every image dyld loads; nothing may come from the build tree."""
    environment = dict(os.environ, DYLD_PRINT_LIBRARIES='1')
    for name in ('DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'LANGLERN_SPEECH_DIR', 'LANGLERN_TESSDATA',
                 'LANGLERN_DICTIONARY', 'LANGLERN_FR_DICTIONARY'):
        environment.pop(name, None)
    command = [app / 'Contents/MacOS/LinguaPDF', '--startup-probe', '--quit-after-ready']
    result = subprocess.run([str(c) for c in command], capture_output=True, text=True, env=environment, timeout=60)
    log = result.stdout + result.stderr
    if result.returncode != 0 or '"interactive_proxy"' not in log:
        raise RuntimeError('Packaged startup probe failed:\n' + log[-4000:])
    # dyld prints one "dyld[<pid>]: <UUID> /path/to/image" line per loaded image.
    loaded = sorted({line.split('> ', 1)[1].strip() for line in log.splitlines()
                     if line.startswith('dyld[') and '> /' in line})
    outside = [path for path in loaded if not path.startswith(('/System/', '/usr/lib/', str(app)))]
    qt = [path for path in loaded if '/QtCore.framework/' in path]
    if outside or not qt:
        raise RuntimeError('Images loaded outside the bundle: ' + '\n'.join(outside or ['(no Qt image seen)']))
    return len(loaded), [line for line in log.splitlines() if '"event"' in line]


def verify_open(app, pdf):
    """Open a PDF in the packaged app (isolated workspace) and wait for the first rendered page."""
    with tempfile.TemporaryDirectory() as workspace:
        environment = dict(os.environ, LANGLERN_WORKSPACE_DIR=workspace)
        process = subprocess.Popen([str(app / 'Contents/MacOS/LinguaPDF'), str(pdf)], stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, env=environment)
        seen = []
        deadline = time.monotonic() + 60
        try:
            for line in process.stdout:
                seen.append(line.rstrip())
                if line.startswith('pdf_frame') or time.monotonic() > deadline:
                    break
        finally:
            process.terminate()
            process.wait(timeout=30)
        frames = [line for line in seen if line.startswith(('pdf_opened', 'pdf_frame'))]
        if not any(line.startswith('pdf_frame') for line in frames):
            raise RuntimeError('Packaged app did not render the sample PDF:\n' + '\n'.join(seen[-40:]))
        return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/macos')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/packages')
    parser.add_argument('--sample-pdf', type=Path, help='PDF opened during the packaged startup check')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise SystemExit('macOS only')
    build = args.build_dir.resolve()
    version = output('plutil', '-extract', 'CFBundleShortVersionString', 'raw', build / 'LinguaPDF.app/Contents/Info.plist').strip()
    folder = args.output_dir.resolve() / f'LinguaPDF-{version}-macos-arm64'
    app = folder / 'LinguaPDF.app'
    if folder.exists():
        shutil.rmtree(folder)  # generated output only
    folder.mkdir(parents=True)
    shutil.copytree(build / 'LinguaPDF.app', app, symlinks=True)
    copy_runtime_data(build, app / 'Contents/Resources')

    run(QT / 'bin/macdeployqt', app, f'-qmldir={ROOT / "qml"}', f'-libpath={build / "pdfium"}', '-always-overwrite')
    # Only SQLite is used (offline dictionaries); other SQL drivers link client libraries not shipped here.
    for driver in (app / 'Contents/PlugIns/sqldrivers').glob('*.dylib'):
        if driver.name != 'libqsqlite.dylib':
            driver.unlink()
    executable = app / 'Contents/MacOS/LinguaPDF'
    for path in rpaths(executable):
        if not path.startswith('@'):
            run('install_name_tool', '-delete_rpath', path, executable)
    if '@executable_path/../Frameworks' not in rpaths(executable):
        run('install_name_tool', '-add_rpath', '@executable_path/../Frameworks', executable)
    leaks = []
    for binary in macho_files(app):
        for line in output('otool', '-L', binary).splitlines()[1:]:
            reference = line.strip().split(' (', 1)[0]
            if reference.startswith('/') and not reference.startswith(('/System/', '/usr/lib/', str(app))):
                leaks.append(f'{binary.relative_to(app)} -> {reference}')
        for path in rpaths(binary):
            if not path.startswith('@'):
                leaks.append(f'{binary.relative_to(app)} rpath {path}')
    if leaks:
        raise RuntimeError('Absolute build paths remain:\n' + '\n'.join(leaks))

    # Ad-hoc signature: nested code first (Frameworks/PlugIns via --deep), then the bundle seal.
    run('codesign', '--force', '--deep', '--sign', '-', '--timestamp=none', app)
    run('codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
    images, events = verify(app)
    opened = verify_open(app, args.sample_pdf) if args.sample_pdf else []

    archive = folder.parent / (folder.name + '.zip')
    archive.unlink(missing_ok=True)
    run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, archive)
    (folder.parent / (archive.name + '.sha256')).write_text(f'{sha(archive)}  {archive.name}\n')
    size = sum(p.stat().st_size for p in app.rglob('*') if p.is_file() and not p.is_symlink())
    print(f'App: {app} ({size / 2**20:.0f} MiB)')
    print(f'Zip: {archive} ({archive.stat().st_size / 2**20:.0f} MiB)')
    print(f'PASS: codesign verify, {images} images loaded only from bundle/system')
    for event in events + opened:
        print('  ' + event)


if __name__ == '__main__':
    main()

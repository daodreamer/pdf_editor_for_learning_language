"""Prepare pinned Supertonic 3 French speech artifacts (no runtime downloads).

Downloads the pinned upstream fp32 graphs, voices and native sources, then derives the shipped graphs
with tools/derive-speech-models.py inside an isolated interpreter pinned by
tools/requirements-speech.lock.txt. Every download and every derived file is checked against
third_party/speech.lock.json; a mismatch stops the build instead of changing the expected values.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / '.deps'
SOURCES = DEPS / 'speech-source'
UPSTREAM = DEPS / 'supertonic3-upstream'
OUTPUT = DEPS / 'supertonic3-fr'
VENV = ROOT / '.tools/speech-venv'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(url, path, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and digest(path) == expected:
        return
    pending = path.with_suffix(path.suffix + '.pending')
    with urllib.request.urlopen(url, timeout=120) as source, pending.open('wb') as output:
        while chunk := source.read(4 * 1024 * 1024):
            output.write(chunk)
    if digest(pending) != expected:
        pending.unlink()
        raise ValueError('Speech dependency checksum mismatch: ' + path.name)
    pending.replace(path)


def verified(path, item):
    return path.exists() and path.stat().st_size == item['bytes'] and digest(path) == item['sha256']


def prepare_onnxruntime(item, platform):
    """Lay out the official NuGet package as the release archive did: include/ and lib/."""
    package = SOURCES / 'microsoft.ml.onnxruntime.1.23.2.nupkg'
    download(item['url'], package, item['sha256'])
    target = item['platforms'][platform]
    directory = SOURCES / target['directory']
    expected = {Path(entry['path']).name: entry for entry in target['lib']}
    if not all(verified(directory / 'lib' / name, entry) for name, entry in expected.items()):
        pending = SOURCES / (target['directory'] + '.pending')
        shutil.rmtree(pending, ignore_errors=True)
        with zipfile.ZipFile(package) as archive:
            for entry in archive.namelist():
                if entry.startswith(item['include']) and not entry.endswith('/'):
                    output = pending / 'include' / entry[len(item['include']):]
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(archive.read(entry))
            for entry in target['lib']:
                output = pending / 'lib' / Path(entry['path']).name
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(archive.read(entry['path']))
                if not verified(output, entry):
                    raise ValueError('ONNX Runtime package file checksum mismatch: ' + entry['path'])
            for notice in ('LICENSE', 'ThirdPartyNotices.txt'):
                (pending / notice).write_bytes(archive.read(notice))
        shutil.rmtree(directory, ignore_errors=True)
        pending.replace(directory)
    # Evidence for the source switch: compare with the previously reviewed release zip when it is still cached.
    legacy = SOURCES / 'onnxruntime-win-x64-1.23.2' / 'lib'
    if platform == 'windows' and legacy.is_dir():
        for name in expected:
            same = (legacy / name).exists() and digest(legacy / name) == digest(directory / 'lib' / name)
            print(f'ONNX Runtime {name}: NuGet copy {"identical to" if same else "DIFFERS from"} the cached release zip')


def derivation_python():
    python = VENV / ('Scripts/python.exe' if sys.platform == 'win32' else 'bin/python')
    requirements = (ROOT / 'tools/requirements-speech.lock.txt').read_text(encoding='utf-8')
    stamp = VENV / 'langlern-requirements.txt'
    if not python.exists() or not stamp.exists() or stamp.read_text(encoding='utf-8') != requirements:
        subprocess.run([sys.executable, '-m', 'venv', '--clear', str(VENV)], check=True)
        subprocess.run([str(python), '-m', 'pip', 'install', '--disable-pip-version-check', '-q',
                        '-r', str(ROOT / 'tools/requirements-speech.lock.txt')], check=True)
        stamp.write_text(requirements, encoding='utf-8')
    return python


def main():
    lock = json.loads((ROOT / 'third_party/speech.lock.json').read_text(encoding='utf-8'))
    platform = 'windows' if sys.platform == 'win32' else 'mac'
    for item in lock['sources']:
        if item['name'] == 'onnxruntime':
            prepare_onnxruntime(item, platform)
            continue
        path = SOURCES / 'soundtouch-2.3.3.tar.gz'
        download(item['url'], path, item['sha256'])
        if not (SOURCES / 'soundtouch').exists():
            with tarfile.open(path) as archive:
                archive.extractall(SOURCES, filter='data')
    model = lock['model']
    upstream = {item['path']: item for item in lock['upstream']}
    for relative, item in upstream.items():
        url = f"https://huggingface.co/{model['repo']}/resolve/{model['commit']}/{relative}?download=true"
        download(url, UPSTREAM / relative, item['sha256'])
    pending = [item for item in lock['files'] if not verified(OUTPUT / item['file'], item)]
    if any('derived_from' in item for item in pending):
        work = DEPS / 'supertonic3-derive.pending'
        shutil.rmtree(work, ignore_errors=True)
        subprocess.run([str(derivation_python()), '-I', str(ROOT / 'tools/derive-speech-models.py'),
                        '--source', str(UPSTREAM / 'onnx'), '--output', str(work)], check=True)
        for item in pending:
            if 'derived_from' in item:
                target = OUTPUT / item['file']
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(work / Path(item['derived_from']).name, target)
        shutil.rmtree(work, ignore_errors=True)
    for item in pending:
        if 'upstream' in item:
            target = OUTPUT / item['file']
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(UPSTREAM / item['upstream'], target)
    for item in lock['files']:
        if not verified(OUTPUT / item['file'], item):
            raise ValueError('Derived speech file checksum mismatch: ' + item['file'])
    for name, expected in lock['notices'].items():
        if digest(ROOT / 'third_party/notices/speech' / name) != expected:
            raise ValueError('Speech notice checksum mismatch: ' + name)
    resource = SOURCES / 'soundtouch/source/SoundTouchDLL/SoundTouchDLL.rc'
    resource.write_bytes(resource.read_bytes().replace(b'afxres.h', b'winresrc.h'))
    print('Pinned Supertonic 3 French voices (M1, F3) and native sources are ready')


if __name__ == '__main__':
    main()

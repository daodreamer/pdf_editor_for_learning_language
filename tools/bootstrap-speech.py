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
    for item in lock['sources']:
        name = item['name']
        filename = {'onnxruntime': 'onnxruntime-win-x64-1.23.2.zip', 'soundtouch': 'soundtouch-2.3.3.tar.gz'}[name]
        path = SOURCES / filename
        download(item['url'], path, item['sha256'])
        directory = {'onnxruntime': 'onnxruntime-win-x64-1.23.2', 'soundtouch': 'soundtouch'}[name]
        if not (SOURCES / directory).exists():
            if name == 'onnxruntime':
                with zipfile.ZipFile(path) as archive:
                    archive.extractall(SOURCES)
            else:
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

"""Build the pinned offline OCR libraries. Run in the project's compiler environment."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(url, path, digest):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or sha(path) != digest:
        pending = path.with_suffix(path.suffix + '.pending')
        urllib.request.urlretrieve(url, pending)
        if sha(pending) != digest:
            raise ValueError('OCR dependency checksum mismatch: ' + path.name)
        pending.replace(path)


def main():
    lock_path = ROOT / 'third_party/ocr.lock.json'
    lock = json.loads(lock_path.read_text())
    prefix = ROOT / '.deps/ocr'
    for model in lock['models']:
        download(model['url'], prefix / 'tessdata' / (model['language'] + '.traineddata'), model['sha256'])
    signature = sha(lock_path) + ':' + sha(Path(__file__))
    stamp = prefix / 'build-lock.sha256'
    if stamp.exists() and stamp.read_text().strip() == signature:
        print('Pinned offline OCR build is ready')
        return
    sources = ROOT / '.deps/ocr-source'
    for item in lock['sources']:
        name, version = item['name'], item['version']
        archive = sources / f'{name}-{version}.tar.gz'
        download(item['url'], archive, item['sha256'])
        source = sources / f'{name}-{version}'
        if not source.exists():
            with tarfile.open(archive) as package:
                package.extractall(sources, filter='data')
        build = ROOT / '.deps/ocr-build' / name
        options = ['-DSW_BUILD=OFF', '-DBUILD_SHARED_LIBS=OFF', '-DCMAKE_BUILD_TYPE=Release',
                   '-DCMAKE_PREFIX_PATH=' + prefix.as_posix(),
                   '-DCMAKE_INSTALL_PREFIX=' + prefix.as_posix()]
        if name == 'zlib':
            options += ['-DZLIB_BUILD_TESTING=OFF', '-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_STATIC=ON']
        elif name == 'libpng':
            options += ['-DPNG_SHARED=OFF', '-DPNG_STATIC=ON', '-DPNG_TESTS=OFF', '-DPNG_TOOLS=OFF',
                        '-DZLIB_LIBRARY=' + (prefix / 'lib' / ('zs.lib' if os.name == 'nt' else 'libz.a')).as_posix()]
        elif name == 'leptonica':
            options += ['-DBUILD_PROG=OFF'] + [f'-DENABLE_{codec}=OFF' for codec in
                                             ('GIF', 'JPEG', 'TIFF', 'WEBP', 'OPENJPEG')]
            options += ['-DENABLE_ZLIB=ON', '-DENABLE_PNG=ON']
            options += ['-DZLIB_LIBRARY=' + (prefix / 'lib' / ('zs.lib' if os.name == 'nt' else 'libz.a')).as_posix()]
        else:
            options += ['-DCMAKE_PREFIX_PATH=' + prefix.as_posix(), '-DOPENMP_BUILD=OFF',
                        '-DBUILD_TRAINING_TOOLS=OFF', '-DBUILD_TESTS=OFF', '-DGRAPHICS_DISABLED=ON',
                        '-DDISABLED_LEGACY_ENGINE=ON', '-DENABLE_NATIVE=OFF', '-DDISABLE_TIFF=ON',
                        '-DCMAKE_CXX_FLAGS=-DTESSERACT_DISABLE_DEBUG_FONTS' + (' /EHsc' if os.name == 'nt' else ''),
                        '-DDISABLE_ARCHIVE=ON', '-DDISABLE_CURL=ON']
        subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', *options], check=True)
        subprocess.run(['cmake', '--build', str(build), '--parallel', '6'], check=True)
        subprocess.run(['cmake', '--install', str(build)], check=True)
    notices = prefix / 'licenses'
    shutil.copytree(ROOT / 'third_party/notices/ocr', notices, dirs_exist_ok=True)
    shutil.copy2(lock_path, prefix / 'ocr.lock.json')
    stamp.write_text(signature + '\n')


if __name__ == '__main__':
    main()

"""Record compiled product inputs after the build script completes successfully."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file,'sha256').hexdigest()

def product_inputs():
    paths=[ROOT/'CMakeLists.txt', ROOT/'third_party/dependencies.lock.json']
    for directory in ('src','qml','assets'):
        paths.extend(p for p in (ROOT/directory).rglob('*') if p.is_file())
    return {p.relative_to(ROOT).as_posix():digest(p) for p in sorted(paths)}

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('build',type=Path)
    args=parser.parse_args()
    cache={}
    for line in (args.build/'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        if line.startswith(('CMAKE_BUILD_TYPE:','CMAKE_CXX_FLAGS:','CMAKE_CXX_FLAGS_RELEASE:',
                            'CMAKE_EXE_LINKER_FLAGS:','CMAKE_EXE_LINKER_FLAGS_RELEASE:')):
            key,value=line.split('=',1); cache[key.split(':')[0]]=value
    result={'source_sha256':product_inputs(), 'exe_sha256':digest(args.build/'langlern.exe'),
            'cmake_cache_sha256':digest(args.build/'CMakeCache.txt'),'configuration':cache}
    (args.build/'product-build.json').write_text(json.dumps(result,indent=2),encoding='utf-8')

"""Replay real Qt/D3D11 reading trajectories; local artifacts, no uploads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--exe', type=Path, default=Path('build/phase3/reading_tests.exe'))
p.add_argument('--pdf', type=Path, required=True)
p.add_argument('--run', type=Path, required=True)
p.add_argument('--source-root', type=Path, help='Source tree used to build the executable (hash manifest only)')
p.add_argument('--seconds', type=int, default=60)
p.add_argument('--repeat', type=int, default=5)
p.add_argument('--workflow', action='store_true', help='30-minute reading/annotation/save/file-switch acceptance')
p.add_argument('--profile', choices=('reading','scrolling','fast','navigation','zoom'), default='reading')
a = p.parse_args()
if a.seconds < 1 or a.repeat < 1:
    p.error('seconds and repeat must be positive')
if a.workflow and a.seconds < 1800:
    p.error('--workflow requires at least 1800 seconds')
if a.workflow and a.profile != 'reading':
    p.error('--workflow requires the reading profile')
root = Path(__file__).resolve().parents[1]
exe, pdf, run = a.exe.resolve(), a.pdf.resolve(), a.run.resolve()
if not exe.is_file() or not pdf.is_file():
    p.error('existing executable and PDF required')
run.mkdir(parents=True, exist_ok=False)
def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()
source = {}
source_root = a.source_root.resolve() if a.source_root else root
for folder in ('src', 'qml', 'tests/shell'):
    for path in (source_root / folder).rglob('*'):
        if path.is_file():
            source[str(path.relative_to(source_root))] = digest(path)
manifest = {'platform': platform.platform(), 'exe_sha256': digest(exe), 'pdf_sha256': digest(pdf),
            'pdf_bytes': pdf.stat().st_size, 'seconds': a.seconds, 'repeat': a.repeat, 'workflow': a.workflow, 'profile': a.profile,
            'source_sha256': source, 'qt': '6.8.3', 'pdfium': '157.0.8100.0',
            'notes': 'Release; warm OS cache after hashing; Qt frameSwapped render event intervals, not scanout. No sanitizer. GPU pixels are estimates.'}
(run / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
env = os.environ.copy()
env['PATH'] = str(root / '.deps/Qt/6.8.3/msvc2022_64/bin') + os.pathsep + env['PATH']
env['LANGLERN_LARGE_PDF'] = str(pdf)
env['LANGLERN_STRESS_SECONDS'] = str(a.seconds)
env['LANGLERN_STRESS_WORKFLOW'] = '1' if a.workflow else '0'
env['LANGLERN_READING_PROFILE'] = a.profile
env['QTEST_FUNCTION_TIMEOUT'] = str((a.seconds + 120) * 1000)
results = []
for i in range(a.repeat):
    trace = run / f'trajectory-{i}.jsonl'
    env['LANGLERN_STRESS_TRACE'] = str(trace)
    start = time.monotonic()
    completed = subprocess.run([str(exe), 'readingStress', '-o', str(run / f'test-{i}.log') + ',txt'],
                               env=env, timeout=a.seconds + 120)
    result = {'index': i, 'returncode': completed.returncode, 'wall_seconds': time.monotonic() - start}
    if trace.exists():
        rows = [json.loads(line) for line in trace.read_text(encoding='utf-8').splitlines()]
        result['begin'] = next((r for r in rows if r['event'] == 'begin'), None)
        result['end'] = next((r for r in rows if r['event'] == 'end'), None)
        samples = [r for r in rows if r['event'] == 'sample']
        for key in ('working_set_bytes', 'private_bytes', 'cpu_image_bytes', 'gpu_pixel_estimate', 'active_pages'):
            values = [r[key] for r in samples if key in r]
            if values:
                result[key] = {'first': values[0], 'last': values[-1], 'peak_sampled': max(values)}
    results.append(result)
    (run / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(result), flush=True)
    if completed.returncode:
        raise SystemExit(completed.returncode)

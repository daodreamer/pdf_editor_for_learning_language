"""Replay Qt mouse input into the real Windows ink UI and preserve local evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, default=Path('build/windows/ink_tests.exe'))
parser.add_argument('--run', type=Path, required=True)
parser.add_argument('--repeat', type=int, default=3)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
exe, run = args.exe.resolve(), args.run.resolve()
if not exe.is_file() or args.repeat < 1:
    parser.error('existing executable and positive repeat required')
run.mkdir(parents=True, exist_ok=False)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


sources = {}
for folder in ('src', 'qml', 'tests/shell'):
    for path in (root / folder).rglob('*'):
        if path.is_file():
            sources[str(path.relative_to(root))] = digest(path)
pdf = root / 'tests/fixtures/scan.pdf'
manifest = {
    'platform': platform.platform(), 'exe_sha256': digest(exe),
    'pdf_sha256': digest(pdf), 'pdf_bytes': pdf.stat().st_size,
    'source_sha256': sources, 'repeat': args.repeat,
    'qt': '6.8.3', 'pdfium': '157.0.8100.0', 'build': 'MSVC x64 Release',
    'notes': 'Injected Qt events, 8192 moves with 64-event bursts and qWait(8). '
             'Handler CPU times and frameSwapped intervals are not hardware input-to-screen latency. '
             'Working set/private are end samples; peak working set is the process lifetime OS peak. '
             'GPU process memory is not sampled. No A/B or cold-start claim.'}
(run / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
env = os.environ.copy()
env['PATH'] = str(root / '.deps/Qt/6.8.3/msvc2022_64/bin') + os.pathsep + env['PATH']
env['QSG_INFO'] = '1'
results = []
for index in range(args.repeat):
    trace, log = run / f'ink-{index + 1}.json', run / f'ink-{index + 1}.log'
    env['LANGLERN_INK_TRACE'] = str(trace)
    result = subprocess.run([str(exe), 'longStrokeTrace', '-o', f'{log},txt'],
                            cwd=root, env=env, capture_output=True, text=True)
    (run / f'ink-{index + 1}.stderr.txt').write_text(result.stderr, encoding='utf-8')
    if result.returncode:
        raise SystemExit(f'ink run {index + 1} failed ({result.returncode}); see {log}')
    data = json.loads(trace.read_text(encoding='utf-8'))
    results.append({key: value for key, value in data.items()
                    if key not in ('input_trace', 'frame_intervals_ms')})
(run / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
print(json.dumps(results, indent=2))

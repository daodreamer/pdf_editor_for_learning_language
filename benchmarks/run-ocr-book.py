"""Whole-book background OCR on the fixed real textbook; local artifacts only, no uploads.

Phases (each a separate langlern_tests process, see tests/shell/ocr_book_tests.cpp):
  1. full:    fresh store, recognise the whole book; wall time, process CPU, memory peak.
  2. resume:  fresh store, SIGKILL after --kill-after pages, restart in the same store and
              finish; then a third open must find the book complete.
  3. scroll:  burst scrolling (3 s scroll / 3 s still) with background OCR off and on (A/B),
              --repeat rounds each, interleaved; Qt frameSwapped intervals while scrolling.
  4. noidle:  optional worst case, background with no idle delay (LANGLERN_OCR_BOOK_IDLE_MS=0).
The PDF is hashed before and after. Results: <run>/summary.json plus per-process traces.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import time

p = argparse.ArgumentParser()
default_exe = 'build/windows/langlern_tests.exe' if sys.platform == 'win32' else 'build/macos/langlern_tests'
p.add_argument('--exe', type=Path, default=Path(default_exe))
p.add_argument('--run', type=Path, required=True, help='new directory under benchmarks/results/')
p.add_argument('--repeat', type=int, default=3)
p.add_argument('--seconds', type=int, default=36, help='scroll phase length per process')
p.add_argument('--kill-after', type=int, default=20)
p.add_argument('--phases', default='full,resume,scroll,noidle')
a = p.parse_args()
pdf = Path(os.environ.get('LANGLERN_REAL_PDF', 'C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf'))
exe, run = a.exe.resolve(), a.run.resolve()
if not exe.is_file() or not pdf.is_file():
    p.error('existing executable and LANGLERN_REAL_PDF required')
run.mkdir(parents=True, exist_ok=False)
phases = set(a.phases.split(','))

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def rows(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line.strip()] if path.exists() else []

pdf_before = digest(pdf)
root = Path(__file__).resolve().parents[1]
git = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, capture_output=True, text=True).stdout.strip()
dirty = subprocess.run(['git', 'status', '--porcelain'], cwd=root, capture_output=True, text=True).stdout.strip() != ''
manifest = {'platform': platform.platform(), 'machine': platform.machine(), 'cpu_count': os.cpu_count(),
            'exe_sha256': digest(exe), 'git_head': git, 'worktree_dirty': dirty,
            'pdf_sha256': pdf_before, 'pdf_bytes': pdf.stat().st_size, 'repeat': a.repeat, 'seconds': a.seconds,
            'notes': 'Release build; warm OS cache after hashing; frame times are Qt frameSwapped intervals (render loop), '
                     'not display scanout; memory/CPU are whole test-process figures (GUI, PDFium, OCR, models).'}
(run / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
summary = {'manifest': manifest}

def launch(name, test, extra, timeout):
    env = os.environ.copy()
    env['LANGLERN_REAL_PDF'] = str(pdf)
    env['LANGLERN_OCR_BOOK_TRACE'] = str(run / f'{name}.jsonl')
    env['QTEST_FUNCTION_TIMEOUT'] = str(timeout * 1000)
    env.update(extra)
    log = (run / f'{name}.log').open('w', encoding='utf-8')
    return subprocess.Popen([str(exe), 'ocrbook', test], env=env, stdout=log, stderr=subprocess.STDOUT), run / f'{name}.jsonl'

def finish(proc, timeout):
    try:
        return proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill(); proc.wait(); return 'timeout'

def full_result(trace):
    r = rows(trace)
    begin = next((x for x in r if x['event'] == 'begin'), {})
    ready = next((x for x in r if x['event'] == 'ready'), {})
    end = next((x for x in r if x['event'] == 'complete'), None)
    samples = [x for x in r if x['event'] == 'sample']
    out = {'ready': ready}
    if end:
        wall = (end['elapsed_ms'] - ready['elapsed_ms']) / 1000
        cpu = end['cpu_seconds'] - begin.get('cpu_seconds', 0)
        out.update({'complete': True, 'wall_seconds_ready_to_complete': wall, 'process_cpu_seconds': cpu,
                    'average_cpu_cores': cpu / max(wall, 1e-9), 'ocr_pages': end['done'], 'native_pages': end['native'],
                    'failed_pages': end['failed'], 'characters': end['characters'], 'store_bytes': end['store_bytes'],
                    'peak_resident_bytes': end.get('peak_resident_bytes'),
                    'peak_footprint_sampled': max((x.get('footprint_bytes', 0) for x in samples), default=None)})
    else:
        out['complete'] = False
    return out

if 'full' in phases:
    workspace = run / 'workspace-full'; workspace.mkdir()
    proc, trace = launch('full', 'benchmarkFullBook', {'LANGLERN_OCR_BOOK_BENCH': 'full', 'LANGLERN_OCR_BOOK_WORKSPACE': str(workspace)}, 3700)
    code = finish(proc, 3700)
    summary['full'] = {'returncode': code, **full_result(trace)}
    print(json.dumps(summary['full']), flush=True)

if 'resume' in phases:
    workspace = run / 'workspace-resume'; workspace.mkdir()
    env = {'LANGLERN_OCR_BOOK_BENCH': 'full', 'LANGLERN_OCR_BOOK_WORKSPACE': str(workspace)}
    proc, trace = launch('resume-1-killed', 'benchmarkFullBook', {**env, 'LANGLERN_OCR_BOOK_STOP_AFTER': str(a.kill_after)}, 3700)
    killed_at = None
    deadline = time.monotonic() + 3600
    while proc.poll() is None and time.monotonic() < deadline:
        stop = next((x for x in rows(trace) if x['event'] == 'ready_to_kill'), None)
        if stop:
            killed_at = stop['done']
            proc.send_signal(signal.SIGKILL if hasattr(signal, 'SIGKILL') else signal.SIGTERM); break
        time.sleep(0.5)
    proc.wait()
    proc2, trace2 = launch('resume-2', 'benchmarkFullBook', env, 3700)
    code2 = finish(proc2, 3700)
    second = full_result(trace2)
    proc3, trace3 = launch('resume-3-reopen', 'benchmarkFullBook', env, 600)
    code3 = finish(proc3, 600)
    third = full_result(trace3)
    summary['resume'] = {'killed_after_pages': killed_at, 'restart_found_stored': second.get('ready', {}).get('stored'),
                         'restart': {'returncode': code2, **second},
                         'reopen_complete_at_ready': third.get('ready', {}).get('complete'),
                         'reopen_ready': third.get('ready'), 'reopen_returncode': code3}
    print(json.dumps(summary['resume']), flush=True)

def scroll(name, mode, extra=None):
    proc, trace = launch(name, 'benchmarkScroll', {'LANGLERN_OCR_BOOK_BENCH': mode, 'LANGLERN_OCR_BOOK_SECONDS': str(a.seconds), **(extra or {})}, a.seconds + 300)
    code = finish(proc, a.seconds + 300)
    r = rows(trace)
    end = next((x for x in r if x['event'] == 'end'), None)
    begin = next((x for x in r if x['event'] == 'begin'), {})
    samples = [x for x in r if x['event'] == 'sample']
    result = {'name': name, 'mode': mode, 'returncode': code, 'enabled': begin.get('enabled'), 'dpr': begin.get('dpr'), 'screen_hz': begin.get('screen_hz')}
    if end:
        cpu = end['cpu_seconds'] - (samples[0]['cpu_seconds'] if samples else 0)
        wall = (end['elapsed_ms'] - (samples[0]['elapsed_ms'] if samples else 0)) / 1000
        result.update({'scroll': end['scroll'], 'still': end['still'], 'book_pages_done': end['book_done'],
                       'background_running_samples_while_scrolling': sum(1 for x in samples if x['scrolling'] and x['book_running']),
                       'average_cpu_cores': cpu / max(wall, 1e-9), 'peak_resident_bytes': end.get('peak_resident_bytes'),
                       'peak_footprint_sampled': max((x.get('footprint_bytes', 0) for x in samples), default=None)})
    print(json.dumps(result), flush=True)
    return result

def aggregate(results):
    import statistics
    ok = [x for x in results if 'scroll' in x]
    if not ok:
        return {}
    pooled = []
    for x in ok:
        pooled += rows(run / f"{x['name']}.jsonl")[-1].get('scroll_raw_ms', [])
    pooled.sort()
    pct = lambda q: pooled[max(0, int(-(-q * len(pooled) // 1)) - 1)] if pooled else None
    return {'runs': len(ok), 'pooled_scroll_frames': len(pooled), 'pooled_p50_ms': pct(.5), 'pooled_p95_ms': pct(.95), 'pooled_p99_ms': pct(.99),
            'pooled_max_ms': pooled[-1] if pooled else None,
            'median_run_p95_ms': statistics.median(x['scroll']['p95_ms'] for x in ok),
            'median_run_p99_ms': statistics.median(x['scroll']['p99_ms'] for x in ok),
            'book_pages_done': [x['book_pages_done'] for x in ok],
            'average_cpu_cores': [round(x['average_cpu_cores'], 3) for x in ok],
            'peak_resident_bytes': max(x['peak_resident_bytes'] or 0 for x in ok)}

if 'scroll' in phases:
    runs = {'off': [], 'on': []}
    for i in range(a.repeat):
        for mode in (('off', 'on') if i % 2 == 0 else ('on', 'off')):
            runs[mode].append(scroll(f'scroll-{mode}-{i}', mode))
    summary['scroll'] = {'runs': runs, 'off': aggregate(runs['off']), 'on': aggregate(runs['on'])}
if 'noidle' in phases:
    runs = [scroll(f'scroll-noidle-{i}', 'on', {'LANGLERN_OCR_BOOK_IDLE_MS': '0'}) for i in range(a.repeat)]
    summary['noidle'] = {'runs': runs, 'aggregate': aggregate(runs)}

summary['pdf_sha256_after'] = digest(pdf)
summary['pdf_unchanged'] = summary['pdf_sha256_after'] == pdf_before
(run / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps({k: v for k, v in summary.items() if k not in ('manifest',)}, default=str)[:4000])

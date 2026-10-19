"""Repeat the fixed-textbook GUI OCR regression and sample process memory (not GPU memory)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time
import psutil
import pymupdf

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--repeat', type=int, default=3)
    args = parser.parse_args()
    args.run.mkdir(parents=True, exist_ok=False)
    sample_path=Path(os.environ.get('LANGLERN_REAL_PDF', 'C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf'))
    sample={}
    if sample_path.exists():
        with sample_path.open('rb') as stream:
            sample={'bytes':sample_path.stat().st_size,'sha256':hashlib.file_digest(stream,'sha256').hexdigest()}
        with pymupdf.open(sample_path) as pdf:
            sample.update(pages=len(pdf),page_34_points=list(pdf[33].rect))
    binary = ROOT / 'build/windows/ocr_selection_tests.exe'
    env = os.environ.copy()
    env['PATH'] = str(ROOT / '.deps/Qt/6.8.3/msvc2022_64/bin') + os.pathsep + env['PATH']
    results = []
    for index in range(args.repeat):
        directory = args.run / str(index + 1)
        directory.mkdir()
        env['LANGLERN_OCR_ARTIFACT_DIR'] = str(directory.resolve())
        log = directory / 'test.txt'
        with (directory / 'stderr.txt').open('wb') as stderr:
            started = time.monotonic_ns()
            process = subprocess.Popen([str(binary), 'realTextbookOcrCopyAndSessionInvalidation', '-o', str(log) + ',txt'],
                                       env=env, stdout=stderr, stderr=stderr)
            monitor = psutil.Process(process.pid)
            samples = []
            while process.poll() is None:
                try:
                    memory = monitor.memory_info()
                    samples.append({'elapsed_ms': (time.monotonic_ns() - started) / 1e6,
                                    'rss_bytes': memory.rss, 'private_bytes': memory.private})
                except psutil.NoSuchProcess:
                    break
                time.sleep(.02)
            code = process.wait()
        (directory / 'memory.json').write_text(json.dumps(samples), encoding='utf-8')
        text = log.read_text(encoding='utf-8', errors='replace')
        def values(pattern):
            return [float(value) for value in re.findall(pattern, text)]
        results.append({'run': index + 1, 'exit_code': code, 'skipped': 'SKIP' in text,
                        'ocr_end_to_end_ms': values(r'real_textbook_ocr_ms=(\d+)'),
                        'ocr_worker_ms': values(r'ocr_result .*?elapsed_ms=([\d.]+)'),
                        'ui_timer_max_gap_ms': values(r'real_textbook_ui_timer_max_gap_ms=(\d+)'),
                        'peak_rss_bytes': max((s['rss_bytes'] for s in samples), default=None),
                        'peak_private_bytes': max((s['private_bytes'] for s in samples), default=None)})
        if code:
            break
    summary = {'os': platform.platform(), 'cpu': platform.processor(), 'logical_cpus': os.cpu_count(), 'sample':sample,
               'dependencies':json.loads((ROOT/'third_party/ocr.lock.json').read_text()),
               'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'results': results,
               'scope': 'Release, warm OS cache, fixed real textbook GUI regression. Memory covers the entire test process; UI timer is an event-loop proxy, not presented frames. No GPU memory or cold startup measurement.'}
    (args.run / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if any(r['exit_code'] or r['skipped'] for r in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()

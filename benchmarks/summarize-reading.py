"""Print audited aggregate facts from a finished run-reading directory."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('run', type=Path)
a = p.parse_args()
results = []
for trace in sorted(a.run.glob('trajectory-*.jsonl')):
    rows = [json.loads(line) for line in trace.read_text(encoding='utf-8').splitlines()]
    samples = [r for r in rows if r.get('event') == 'sample']
    end = next((r for r in rows if r.get('event') == 'end'), None)
    result = {'trajectory': trace.name, 'finished': end is not None, 'sample_count': len(samples), 'end': end}
    if samples:
        last = samples[-1]
        result['last_sample_seconds'] = last['elapsed_ms'] / 1000
        for metric in ('cpu_image_bytes', 'gpu_pixel_estimate', 'working_set_bytes', 'private_bytes', 'active_pages'):
            values = [r[metric] for r in samples if metric in r]
            if values:
                result[metric] = {'peak_sampled': max(values), 'last': values[-1]}
        lookups = last.get('cache_lookups', 0)
        result['cache_lookups'] = lookups
        result['exact_hit_fraction'] = last.get('exact_hits', 0) / lookups if lookups else None
        result['degraded_hit_fraction'] = last.get('degraded_hits', 0) / lookups if lookups else None
        result['evictions'] = last.get('evictions')
        result['unique_sampled_pages'] = len({r['page'] for r in samples})
    results.append(result)
gpu_path = a.run / 'gpu-process.jsonl'
gpu = None
if gpu_path.exists():
    rows = [json.loads(line) for line in gpu_path.read_text(encoding='utf-8').splitlines()]
    gpu = {'samples': len(rows), 'errors': sum('error' in r for r in rows)}
    for metric in ('dedicated usage', 'shared usage', 'total committed'):
        values = [r[metric] for r in rows if isinstance(r.get(metric), (int, float))]
        gpu[metric] = {'peak_sampled': max(values), 'last': values[-1]} if values else None
print(json.dumps({'runs': results, 'gpu_process_memory': gpu}, indent=2))

import argparse
import json
from pathlib import Path
import re

p = argparse.ArgumentParser()
p.add_argument("log", type=Path)
a = p.parse_args()
text = a.log.read_text(encoding="utf-8-sig")
traces = {d["generation"]: d for d in (json.loads(x) for x in re.findall(r"worker_trace (\{.*\})", text))}
requests = {int(generation): int(ns) for ns, generation in re.findall(r"request_ns (\d+) generation (\d+)", text)}
summary = {"trace_generations": sorted(traces), "discarded_2": "discarded 2" in text,
    "actual_d3d11": "Creating QRhi with backend D3D11" in text,
    "resource_rebuild_count": text.count("Creating QRhi with backend D3D11"),
    "synthetic_ime_pass": "synthetic_ime_preedit_ok true commit_ok true" in text,
    "screenshot_saved": "screenshot_saved true" in text,
    "first_frame_submitted_ms": float(re.search(r"first_frame_submitted_ms ([\d.]+)", text).group(1)),
    "image_delivered_ms": dict((generation, float(ms)) for ms, generation in re.findall(r"image_delivered_ms ([\d.]+) generation (\d+)", text)),
    "note": "one diagnostic run; no system presentation, interactive-startup or FPS claim"}
if 2 in traces and 3 in traces and 3 in requests:
    summary["replacement_to_next_task_start_ms"] = (int(traces[3]["started_ns"])-requests[3])/1e6
    summary["stale_task_full_occupancy_ms"] = traces[2]["total_worker_ms"]
    summary["replacement_during_task_2"] = int(traces[2]["started_ns"]) <= requests[3] <= int(traces[2]["finished_ns"])
a.log.with_suffix(".summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))

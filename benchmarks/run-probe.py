"""Local-only worker benchmark; process RSS is not GPU memory or presented frame time."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import shutil
import statistics
import psutil

p = argparse.ArgumentParser()
p.add_argument("pdf", type=Path)
p.add_argument("--run", required=True, type=Path)
p.add_argument("--width", type=int, default=1400)
p.add_argument("--repeat", type=int, default=33)
p.add_argument("--sample-id", default="user-local-textbook")
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
a.run.mkdir(parents=True, exist_ok=False)
env = os.environ.copy()
qt = root / ".deps/Qt/6.8.3/msvc2022_64"
env["PATH"] = str(qt / "bin") + os.pathsep + env["PATH"]
exe = root / "build/windows/phase0_pdf_probe.exe"
def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()
before = sha(a.pdf)
cmd = [str(exe), str(a.pdf.resolve()), "--output", str(a.run.resolve()),
       "--width", str(a.width), "--repeat", str(a.repeat)]
start = time.monotonic_ns()
samples = []
with (a.run / "stdout.log").open("xb") as out:
    process = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env)
    monitored = psutil.Process(process.pid)
    while process.poll() is None:
        try:
            m = monitored.memory_info()
            samples.append({"elapsed_ms": (time.monotonic_ns()-start)/1e6, "rss_bytes": m.rss,
                            "private_bytes": getattr(m, "private", None),
                            "handles": monitored.num_handles()})
        except psutil.NoSuchProcess:
            break
        time.sleep(.01)
after = sha(a.pdf)
assert before == after, "source changed"
if process.returncode:
    raise SystemExit(f"probe failed ({process.returncode}); use a fresh run directory; see stdout.log")
(a.run / "private").mkdir(exist_ok=True)
if (a.run / "page.png").exists():
    shutil.move(str(a.run / "page.png"), str(a.run / "private/page.png"))
rows = [json.loads(line) for line in (a.run / "events.jsonl").read_text().splitlines()]
formal = rows[3:]
def stats(key):
    values = sorted(row[key] for row in formal if key in row)
    if not values:
        return None
    import math
    return {"count": len(values), "median": statistics.median(values),
            "p95": values[math.ceil(.95*len(values))-1], "min": values[0], "max": values[-1]}
summary = {"exit_code": process.returncode, "source_sha256_unchanged": before == after,
           "warmup_count": 3, "formal_count": len(formal), "os_cache": "warm after source hash; cold unmeasured",
           "open_ms": stats("open_ms"), "render_ms": stats("render_ms"), "total_worker_ms": stats("total_worker_ms"),
           "peak_sampled_rss_bytes": max((s["rss_bytes"] for s in samples), default=0),
           "sampling_interval_ms": 10, "gpu_memory": "unmeasured", "ui_frame_time": "unmeasured"}
def write_new(name, value):
    with (a.run / name).open("x", encoding="utf-8") as f:
        json.dump(value, f, indent=2)
write_new("resources.json", samples)
write_new("summary.json", summary)
write_new("manifest.json", {"sample_id": a.sample_id, "sample_sha256": before,
    "sample_bytes": a.pdf.stat().st_size, "platform": platform.platform(), "cpu": platform.processor(),
    "ram_bytes": psutil.virtual_memory().total, "qt": "6.8.3", "pdfium": "157.0.8100.0",
    "build": "Release / MSVC 19.51 / C++20", "width": a.width, "repeat": a.repeat,
    "notes": "No document path or extracted text recorded. RSS sampled, may miss short peaks. Hash read warmed file cache."})
print(json.dumps(summary, indent=2))
raise SystemExit(process.returncode)

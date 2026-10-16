"""Measure fresh-process warm startup submission/input proxies, never cold boot or OS input."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import threading
import time

import psutil

parser = argparse.ArgumentParser()
parser.add_argument("--exe", type=Path, default=Path("build/windows/langlern.exe"))
parser.add_argument("--run", type=Path, required=True)
parser.add_argument("--repeat", type=int, default=33)
parser.add_argument("--control-exe", type=Path)
parser.add_argument("--control-source-root", type=Path)
args = parser.parse_args()
if bool(args.control_exe) != bool(args.control_source_root):
    parser.error("control executable and its actual source root must be supplied together")
if args.repeat < 4:
    parser.error("--repeat must include 3 warmups and at least 1 measured run")
args.run.mkdir(parents=True, exist_ok=False)
root = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env["PATH"] = str(root / ".deps/Qt/6.8.3/msvc2022_64/bin") + os.pathsep + env["PATH"]
env["QT_FORCE_STDERR_LOGGING"] = "1"
env["QT_MESSAGE_PATTERN"] = "%{message}"
rows = []
variants = [("optimized", args.exe)]
if args.control_exe:
    variants.append(("control", args.control_exe))
for index, variant, executable in [(i,v,e) for i in range(args.repeat) for v,e in (variants if i%2==0 else variants[::-1])]:
    start = time.monotonic_ns()
    process = subprocess.Popen([str(executable.resolve()), "--startup-probe", "--quit-after-ready"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    watchdog = threading.Timer(15, process.kill)
    watchdog.start()
    events = []
    raw = bytearray()
    try:
        for line in process.stdout:
            received = (time.monotonic_ns() - start) / 1e6
            raw.extend(line)
            try:
                event = json.loads(line)
            except (ValueError, UnicodeDecodeError):
                continue
            event["external_received_ms"] = received
            events.append(event)
        process.wait(timeout=5)
    finally:
        watchdog.cancel()
        if process.poll() is None:
            process.kill()
            process.wait()
    (args.run / f"{variant}-startup-{index:02}.log").write_bytes(raw)
    interactive = [e for e in events if e.get("event") == "interactive_proxy"]
    if process.returncode or len(interactive) != 1:
        raise RuntimeError(f"run {index} failed: exit {process.returncode}; see log")
    rows.append({"index": index, "variant": variant, "warmup": index < 3, "events": events})

def stats(values):
    values = sorted(values)
    return {"count": len(values), "median": statistics.median(values),
            "p95": values[math.ceil(.95 * len(values))-1], "min": values[0], "max": values[-1]}

formal = [e for row in rows if not row['warmup'] and row['variant']=='optimized' for e in row["events"] if e["event"] == "interactive_proxy"]
build_types = sorted({e["build_type"] for row in rows for e in row["events"]})
graphics_apis = sorted({e["graphics_api"] for row in rows for e in row["events"] if "graphics_api" in e})
qt_versions = sorted({e["qt"] for row in rows for e in row["events"] if "qt" in e})
if len(build_types) != 1 or len(graphics_apis) != 1 or len(qt_versions) != 1:
    raise RuntimeError("inconsistent or missing build/backend/Qt metadata; raw logs retained")
files = [root / "CMakeLists.txt"] + sorted((root / "src").rglob("*")) + sorted((root / "qml").rglob("*"))
manifest = {"platform": platform.platform(), "cpu": platform.processor(), "logical_cpus": psutil.cpu_count(),
            "ram_bytes": psutil.virtual_memory().total, "exe_sha256": hashlib.sha256(args.exe.read_bytes()).hexdigest(),
            "source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in files if p.is_file()},
            "build_types": build_types, "qt_versions": qt_versions, "graphics_api_enum": graphics_apis,
            "configuration": "per-event JSON logging; synthetic F6; no screenshots",
            "cache": "warm OS and Qt caches; 3 discarded warmups; fresh process every run",
            "cold_start": "not measured; no OS cache eviction or reboot",
            "external_metric": "launch request to receipt of Qt first-frame + synthetic shortcut acknowledgement; includes pipe/driver latency",
            "system_input_and_presentation": "not measured", "warmup_count": 3}
summary = {"external_interactive_proxy_ms": stats([e["external_received_ms"] for e in formal]),
           "main_interactive_proxy_ms": stats([e["main_ms"] for e in formal])}
if args.control_exe:
    tree=args.control_source_root.resolve()
    control_files=[tree/'CMakeLists.txt']+sorted((tree/'src').rglob('*'))+sorted((tree/'qml').rglob('*'))
    manifest['control']={'exe_sha256':hashlib.sha256(args.control_exe.read_bytes()).hexdigest(),
                         'source_sha256':{str(p.relative_to(tree)):hashlib.sha256(p.read_bytes()).hexdigest()
                                          for p in control_files if p.is_file()},
                         'order':'Alternating AB/BA fresh processes; 3 warmups and 30 measured runs per variant by default'}
    control=[e for row in rows if not row['warmup'] and row['variant']=='control'
             for e in row['events'] if e['event']=='interactive_proxy']
    summary['control']={'external_interactive_proxy_ms':stats([e['external_received_ms'] for e in control]),
                        'main_interactive_proxy_ms':stats([e['main_ms'] for e in control])}
for name, value in [("runs.json", rows), ("manifest.json", manifest), ("summary.json", summary)]:
    with (args.run / name).open("x", encoding="utf-8") as file:
        json.dump(value, file, indent=2, ensure_ascii=False)
print(json.dumps(summary, indent=2))

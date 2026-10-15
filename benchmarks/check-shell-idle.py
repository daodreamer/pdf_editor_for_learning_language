"""Single Windows idle sample of an owned child process; no UI automation."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import psutil

p = argparse.ArgumentParser()
p.add_argument("--exe", type=Path, required=True)
p.add_argument("--run", type=Path, required=True)
a = p.parse_args()
a.run.mkdir(parents=True, exist_ok=False)
root = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env["PATH"] = str(root / ".deps/Qt/6.8.3/msvc2022_64/bin") + os.pathsep + env["PATH"]
env["QT_FORCE_STDERR_LOGGING"] = "1"
env["QT_MESSAGE_PATTERN"] = "%{message}"
with (a.run / "idle.log").open("xb") as log:
    child = subprocess.Popen([str(a.exe.resolve())], stdout=log, stderr=subprocess.STDOUT, env=env)
    try:
        proc = psutil.Process(child.pid)
        time.sleep(2)  # outside the measured idle interval
        if child.poll() is not None:
            raise RuntimeError("child exited before idle sample")
        cpu0 = proc.cpu_times()
        t0 = time.monotonic_ns()
        time.sleep(5)
        cpu1 = proc.cpu_times()
        elapsed = (time.monotonic_ns() - t0) / 1e9
        memory = proc.memory_info()
        modules = sorted({Path(m.path).name for m in proc.memory_maps() if m.path.lower().endswith(".dll")})
        result = {"interval_seconds": elapsed, "cpu_seconds": cpu1.user + cpu1.system - cpu0.user - cpu0.system,
                  "cpu_percent_one_core": 100 * (cpu1.user + cpu1.system - cpu0.user - cpu0.system) / elapsed,
                  "rss_bytes_at_end": memory.rss, "private_bytes_at_end": getattr(memory, "private", None),
                  "modules": modules, "pdfium_loaded": any("pdfium" in m.lower() for m in modules),
                  "scope": "one sample; no document; foreground window; not a peak-memory or GPU measurement",
                  "cleanup": "owned child terminated after sampling; normal close covered by Qt interaction test"}
    finally:
        if child.poll() is None:
            child.terminate()
        child.wait(timeout=10)
with (a.run / "idle.json").open("x", encoding="utf-8") as output:
    json.dump(result, output, indent=2)
print(json.dumps({k: v for k, v in result.items() if k != "modules"}, indent=2))

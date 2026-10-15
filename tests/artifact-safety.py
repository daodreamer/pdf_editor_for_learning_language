"""Regression: GUI screenshot and benchmark logs must never truncate a PDF alias."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=False)
source = root / "tests/fixtures/geometry.pdf"
before = hashlib.sha256(source.read_bytes()).hexdigest()
shot = out / "source-alias.png"
os.link(source, shot)
env = os.environ.copy()
env["PATH"] = str(root / ".deps/Qt/6.8.3/msvc2022_64/bin") + os.pathsep + env["PATH"]
env["QT_FORCE_STDERR_LOGGING"] = "1"
quick = subprocess.run([str(root / "build/windows/phase0_quick_probe.exe"), str(source), str(shot)],
                       env=env, capture_output=True, timeout=30)
assert quick.returncode == 1, quick.stderr
assert b"screenshot_saved false" in quick.stderr
assert hashlib.sha256(source.read_bytes()).hexdigest() == before
os.link(source, out / "stdout.log")
benchmark = subprocess.run([sys.executable, str(root / "benchmarks/run-probe.py"), str(source),
                            "--run", str(out)], env=env, capture_output=True, timeout=10)
assert benchmark.returncode != 0
assert hashlib.sha256(source.read_bytes()).hexdigest() == before
(out / "safety.json").write_text(json.dumps({"quick_screenshot_hardlink_rejected": True,
    "existing_benchmark_directory_rejected": True, "source_sha256_unchanged": True}, indent=2))
print("PASS: GUI screenshot hardlink and benchmark existing-directory safety")

"""Fetch locked development dependencies into this repository; no global configuration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.request

p = argparse.ArgumentParser()
p.add_argument("--platform", choices=("windows", "mac"), required=True)
p.add_argument("--pdfium-only", action="store_true", help="Fetch and verify only the locked PDFium archive")
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
lock = json.loads((root / "third_party/dependencies.lock.json").read_text())
def download(url, target, expected):
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        urllib.request.urlretrieve(url, target)
    with target.open("rb") as f:
        actual = hashlib.file_digest(f, "sha256").hexdigest()
    if actual != expected:
        raise RuntimeError(f"SHA256 mismatch: {target.name}")
for entry in lock["archives"]:
    wanted = "pdfium-win-x64.tgz" if a.platform == "windows" else "pdfium-mac-arm64.tgz"
    if entry["file"] == wanted:
        archive = root / ".deps/downloads" / entry["file"]
        download(entry["url"], archive, entry["sha256"])
        dest = root / ".deps" / ("pdfium-win" if a.platform == "windows" else "pdfium-mac")
        dest.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            tar.extractall(dest, filter="data")
if a.pdfium_only:
    print("Locked PDFium ready.")
    sys.exit(0)
qt_dest = root / ".deps/Qt"
arch = "win64_msvc2022_64" if a.platform == "windows" else "clang_64"
host = "windows" if a.platform == "windows" else "mac"
subprocess.run([sys.executable, "-m", "aqt", "install-qt", host, "desktop", "6.8.3", arch,
                "-O", str(qt_dest), "--archives", "qtbase", "qtdeclarative", "qttools",
                "--keep", "--archive-dest", str(root / ".deps/downloads")], check=True)
if a.platform == "windows":
    for entry in lock["archives"]:
        if entry["file"].endswith(".7z"):
            download(entry["url"], root / ".deps/downloads" / entry["file"], entry["sha256"])
# mac Qt archive hashes remain unverified until a real platform run; never claim a full mac lock.
f = lock["font"]
font_dir = root / ".deps/fonts"
download(f["url"], font_dir / "NotoSansSC-VF.ttf", f["sha256"])
subprocess.run([sys.executable, "-m", "fontTools.varLib.instancer", str(font_dir / "NotoSansSC-VF.ttf"),
                "wght=400", "--output", str(font_dir / "NotoSansSC-Regular.ttf")], check=True)
print("Development dependencies ready. Distribution review remains separate.")

"""Capture installed artifact identities and vendor SBOM licensing evidence."""
from pathlib import Path
import hashlib
import json
import re

root = Path(__file__).resolve().parents[1]
out = root / "third_party"
def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()
archives = []
qtbase = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_msvc2022_64/6.8.3-0-202503201308"
for f in sorted((root / ".deps/downloads").iterdir()):
    url = ("https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8100/" + f.name
           if f.suffix == ".tgz" else qtbase + f.name)
    archives.append({"file": f.name, "url": url, "bytes": f.stat().st_size, "sha256": sha(f)})
font = root / ".deps/fonts/NotoSansSC-VF.ttf"
derived = root / ".deps/fonts/NotoSansSC-Regular.ttf"
lock = {"recorded": "2026-10-15", "qt": "6.8.3", "qt_minimum": "6.8.3",
        "pdfium": "157.0.8100.0", "pdfium_commit": "f51279699cb59754d1f49aef10eb8e825fe56532",
        "pdfium_binary_source": "https://github.com/bblanchon/pdfium-binaries/tree/chromium/8100",
        "archives": archives, "font": {"commit": "f8d157532fbfaeda587e826d4cd5b21a49186f7c",
        "url": "https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/Variable/TTF/Subset/NotoSansSC-VF.ttf",
        "sha256": sha(font), "regular_sha256": sha(derived),
        "derivation": "fonttools 4.60.1 varLib.instancer wght=400 (development only)"},
        "tool_versions": {"python": "3.13.5", "cmake": "3.31.6", "ninja": "1.11.1.3", "aqtinstall": "3.3.0"}}
(out / "dependencies.lock.json").write_text(json.dumps(lock, indent=2) + "\n")
sboms = []
for f in sorted((root / ".deps/Qt/6.8.3/msvc2022_64/sbom").glob("*.spdx.json")):
    data = json.loads(f.read_text(encoding="utf-8"))
    packages = [{k: item[k] for k in ("name", "versionInfo", "downloadLocation", "licenseConcluded",
                 "licenseDeclared", "copyrightText", "sourceInfo", "comment", "attributionTexts") if k in item}
                for item in data.get("packages", [])]
    sboms.append({"artifact": f.name, "sha256": sha(f), "packages": packages,
                  "extracted_license_texts": data.get("hasExtractedLicensingInfos", [])})
(out / "qt-sbom-evidence.json").write_text(json.dumps(sboms, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
notices = [{"file": str(f.relative_to(out)).replace("\\", "/"), "sha256": sha(f)}
           for f in sorted((out / "notices").rglob("*")) if f.is_file()]
(out / "notices.sha256.json").write_text(json.dumps(notices, indent=2) + "\n")
print(f"Recorded {len(archives)} archives, {sum(len(s['packages']) for s in sboms)} SBOM packages, {len(notices)} notices")

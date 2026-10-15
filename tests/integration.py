"""Real PDFium integration + independent MuPDF inspection; run after make-fixtures.py."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import pymupdf

p = argparse.ArgumentParser()
p.add_argument("--exe", type=Path, required=True)
p.add_argument("--font", type=Path, required=True)
p.add_argument("--out", type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
a.out.mkdir(parents=True, exist_ok=True)
pdf = root / "tests/fixtures/geometry.pdf"
before = hashlib.sha256(pdf.read_bytes()).hexdigest()
records = []
for page in range(8):
    folder = a.out / f"page-{page}"
    cmd = [str(a.exe.resolve()), str(pdf), "--page", str(page), "--output", str(folder), "--width", "1000"]
    export = folder / "export.pdf"
    if page in (0, 1, 4):
        if export.exists():
            export.unlink()  # Exact generated fixture output under this test's output directory.
        cmd += ["--font", str(a.font.resolve()), "--export", str(export)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    meta = json.loads(proc.stdout)
    assert meta["coordinate_max_pixel_error"] <= 1
    assert meta["text_characters"] > 0
    assert meta["rotation"] == page % 4
    if export.exists():
        d = pymupdf.open(export)
        extracted = d[page].get_text().replace("\u00a0", " ")
        assert "Bonjour \u4e2d\u6587\u7ec3\u4e60" in extracted, ascii(extracted)
        paths = d[page].get_drawings()
        stroke = [x for x in paths if x.get("width") is not None and
                  (abs(x["width"]-3) < .01 or abs(x["width"]-6) < .01)]
        assert stroke, "stroke width lost"
        assert any(abs(x["stroke_opacity"]-128/255) < .01 for x in stroke), "stroke alpha lost"
        d[page].get_pixmap().save(folder / "mupdf.png")
        meta["independent_mupdf_text_and_path"] = True
        meta["export_sha256"] = hashlib.sha256(export.read_bytes()).hexdigest()
    if page == 0:
        # PDFium PNG: channel and alpha checks on authored color rectangles.
        pix = pymupdf.Pixmap(str(folder / "page.png"))
        def pixel(x, y):
            return pix.pixel(round((x-10)/570*pix.width), round((800-y)/780*pix.height))[:3]
        assert pixel(80, 650) == (255, 0, 0)
        assert pixel(200, 650) == (0, 255, 0)
        assert pixel(320, 650) == (0, 0, 255)
        assert all(abs(v-e) <= 1 for v, e in zip(pixel(440, 650), (255, 127, 127)))
        meta["bgra_alpha_color_checks"] = True
    records.append(meta)
for args, expected in [([str(root / "tests/fixtures/damaged.pdf")], 1),
                       ([str(pdf), "--width", "999999"], 1),
                       ([str(pdf), "--export", str(pdf), "--font", str(a.font.resolve())], 1),
                       ([str(pdf), "--page", "9999"], 1)]:
    proc = subprocess.run([str(a.exe.resolve()), *args], capture_output=True, text=True)
    assert proc.returncode == expected, proc.stdout
    records.append({"negative_case": args[1:] if len(args) > 1 else ["damaged"], "exit_code": proc.returncode})
burst = subprocess.run([str(a.exe.resolve()), str(pdf), "--burst"], capture_output=True, text=True)
assert burst.returncode == 0
assert json.loads(burst.stdout)["generation"] == 100
assert hashlib.sha256(pdf.read_bytes()).hexdigest() == before
records.append({"latest_only_burst": 100, "source_sha256_unchanged": True})
# Verify diagnostic targets cannot truncate hard-linked input files.
for name in ("events.jsonl", "page.png"):
    folder = a.out / ("collision-" + name)
    folder.mkdir()
    os.link(pdf, folder / name)
    proc = subprocess.run([str(a.exe.resolve()), str(pdf), "--output", str(folder)], capture_output=True, text=True)
    assert proc.returncode != 0
    assert hashlib.sha256(pdf.read_bytes()).hexdigest() == before
records.append({"diagnostic_hardlink_collisions_rejected": True})
(a.out / "integration.json").write_text(json.dumps(records, indent=2))
print(f"PASS: 8 geometry pages, 3 independent exports, 4 error cases, 100 demand replacements")

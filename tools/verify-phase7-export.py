"""Development-only independent MuPDF checks; never linked or shipped in the app."""
import argparse
import hashlib
import json
import unicodedata
from pathlib import Path

import fitz

parser = argparse.ArgumentParser()
parser.add_argument("run_dir", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
run = args.run_dir.resolve()
source_path = root / "tests/fixtures/text.pdf"
source_hash = hashlib.sha256(source_path.read_bytes()).hexdigest()
source = fitz.open(source_path)
exported = fitz.open(run / "annotated.pdf")
sparse = fitz.open(run / "sparse.pdf")
assert len(source) == len(exported) == len(sparse) == 8
report = {"engine": f"MuPDF {fitz.VersionBind}", "source_sha256": source_hash, "pages": []}
for index, page in enumerate(exported):
    text = unicodedata.normalize("NFC", page.get_text())
    # Font/style runs may be split into separate extracted lines by MuPDF.
    compact = "".join(text.split())
    assert "Bonjour中文练习é" in compact, (index, text)
    assert "第二行answer" in compact, (index, text)
    assert "EDIT AFTER EXPORT" not in text
    fonts = page.get_fonts(full=True)
    embedded = [f for f in fonts if f[2] == "Type0" and "Noto" in f[3]]
    assert embedded, fonts
    assert all(exported.extract_font(f[0])[3] for f in embedded)
    assert page.rotation == source[index].rotation
    assert page.rect == source[index].rect
    assert len(page.get_images(full=True)) == len(source[index].get_images(full=True))
    drawings = page.get_drawings()
    assert len(drawings) >= 2
    assert any(d.get("fill") == (0.0, 0.0, 1.0) for d in drawings)
    if index == 0:
        assert any(abs(d.get("fill_opacity", 0) - 128 / 255) < .01 for d in drawings)
    before = source[index].get_pixmap(matrix=fitz.Matrix(1, 1), alpha=False)
    after = page.get_pixmap(matrix=fitz.Matrix(1, 1), alpha=False)
    assert before.samples != after.samples
    # Untouched pages preserve the actual original vector streams and pixels.
    if index > 0:
        assert sparse[index].read_contents() == source[index].read_contents()
        assert sparse[index].get_pixmap(alpha=False).samples == before.samples
    report["pages"].append({"page": index + 1, "rotation": page.rotation,
                            "physical_size": list(page.rect), "text_roundtrip": True,
                            "font_embedded": True, "unchanged_images": True,
                            "untouched_sparse_page_identical": index > 0})
    if index in (0, 1, 4, 7):
        destination = run / f"mupdf-page-{index+1}.png"
        if destination.exists():
            raise FileExistsError(destination)
        page.get_pixmap(matrix=fitz.Matrix(1, 1), alpha=False).save(destination)
assert hashlib.sha256(source_path.read_bytes()).hexdigest() == source_hash
report["artifacts"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in run.iterdir() if p.suffix in (".pdf", ".json", ".png")}
destination = run / "independent-verification.json"
with destination.open("x", encoding="utf-8", newline="\n") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)
print(json.dumps({"result": "passed", "pages": 8, "untouched_pages": 7,
                  "source_unchanged": True, "report": str(destination)}, ensure_ascii=False))

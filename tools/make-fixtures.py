"""Deterministic original PDF fixture, generated using Python stdlib only (MIT)."""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parents[1]
target = root / "tests/fixtures"
target.mkdir(parents=True, exist_ok=True)
objects = [b"<< /Type /Catalog /Pages 2 0 R >>", b"", b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
pages = []
for unit in (1, 2):
    for rotation in (0, 90, 180, 270):
        page_id = len(objects) + 1
        stream_id = page_id + 1
        pages.append(page_id)
        objects.append((f"<< /Type /Page /Parent 2 0 R /MediaBox [-20 -30 600 820] "
                        f"/CropBox [10 20 580 800] /Rotate {rotation} /UserUnit {unit} "
                        f"/Resources << /Font << /F1 3 0 R >> /ExtGState << /GS1 << /ca 0.5 >> >> >> "
                        f"/Contents {stream_id} 0 R >>").encode())
        content = (b"1 0 0 rg 40 600 100 100 re f\n0 1 0 rg 160 600 100 100 re f\n"
                   b"0 0 1 rg 280 600 100 100 re f\nq /GS1 gs 1 0 0 rg 400 600 100 100 re f Q\n"
                   b"0 0 0 rg BT /F1 24 Tf 40 740 Td (Phase zero - Bonjour) Tj ET\n"
                   b"0 0 0 RG 2 w 40 80 m 300 80 l S\n")
        objects.append(f"<< /Length {len(content)} >>\nstream\n".encode() + content + b"endstream")
objects[1] = f"<< /Type /Pages /Count {len(pages)} /Kids [{' '.join(f'{p} 0 R' for p in pages)}] >>".encode()
pdf = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
offsets = [0]
for i, obj in enumerate(objects, 1):
    offsets.append(len(pdf))
    pdf += f"{i} 0 obj\n".encode() + obj + b"\nendobj\n"
xref = len(pdf)
pdf += f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode()
for offset in offsets[1:]:
    pdf += f"{offset:010} 00000 n \n".encode()
pdf += f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
(target / "geometry.pdf").write_bytes(pdf)
(target / "damaged.pdf").write_bytes(b"%PDF-1.7\nintentionally truncated")
(target / "manifest.json").write_text(json.dumps({"source": "original deterministic synthetic fixture, MIT",
    "generator": "tools/make-fixtures.py", "pages": 8, "bytes": len(pdf),
    "sha256": hashlib.sha256(pdf).hexdigest(), "text_layer": True,
    "rotations": [0, 90, 180, 270], "user_units": [1, 2], "crop_box": [10, 20, 580, 800]}, indent=2), encoding="utf-8")
print(target)

"""Synthetic large-file stress, NOT a representative scan textbook/codec workload.
800 distinct deterministic 1024x1024 raw grayscale images; local artifact only.
"""
import argparse
from pathlib import Path
import random

p = argparse.ArgumentParser()
p.add_argument("output", type=Path)
p.add_argument("--pages", type=int, default=800)
a = p.parse_args()
a.output.parent.mkdir(parents=True, exist_ok=True)
rng = random.Random(20261015)
offsets = [0]
with a.output.open("xb") as f:
    f.write(b"%PDF-1.7\n% synthetic raw-image stress, not a real scanned textbook\n")
    def obj(data):
        index = len(offsets)
        offsets.append(f.tell())
        f.write(f"{index} 0 obj\n".encode() + data + b"\nendobj\n")
    obj(b"<< /Type /Catalog /Pages 2 0 R >>")
    kids = " ".join(f"{3+3*i} 0 R" for i in range(a.pages))
    obj(f"<< /Type /Pages /Count {a.pages} /Kids [{kids}] >>".encode())
    for i in range(a.pages):
        page, content, image = 3+3*i, 4+3*i, 5+3*i
        obj(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /XObject << /I {image} 0 R >> >> /Contents {content} 0 R >>".encode())
        stream = b"q 612 0 0 792 0 0 cm /I Do Q"
        obj(f"<< /Length {len(stream)} >>\nstream\n".encode() + stream + b"\nendstream")
        pixels = rng.randbytes(1024*1024)
        obj(f"<< /Type /XObject /Subtype /Image /Width 1024 /Height 1024 /ColorSpace /DeviceGray /BitsPerComponent 8 /Length {len(pixels)} >>\nstream\n".encode()+pixels+b"\nendstream")
    xref = f.tell()
    f.write(f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode())
    for offset in offsets[1:]:
        f.write(f"{offset:010} 00000 n \n".encode())
    f.write(f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode())
print(a.output.stat().st_size)

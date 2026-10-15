"""Original MIT selection fixture. No embedded/distributed font, stdlib only."""
from pathlib import Path
import hashlib
import json

target = Path(__file__).resolve().parents[1] / 'tests/fixtures'
objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'',
           b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /ToUnicode 4 0 R >>']
cmap = b'''/CIDInit /ProcSet findresource begin 12 dict begin begincmap
/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def
/CMapName /SelectionUnicode def /CMapType 2 def
1 begincodespacerange <00> <FF> endcodespacerange
1 beginbfrange <20> <7E> <0020> endbfrange
5 beginbfchar <80> <4E2D> <81> <6587> <82> <D83DDE00> <83> <00650301> <84> <FB01> endbfchar
endcmap CMapName currentdict /CMap defineresource pop end end'''
objects.append(f'<< /Length {len(cmap)} >>\nstream\n'.encode()+cmap+b'\nendstream')
pages = []
for unit in (1, 2):
    for rotation in (0, 90, 180, 270):
        page = len(objects)+1
        pages.append(page)
        objects.append((f'<< /Type /Page /Parent 2 0 R /MediaBox [-20 -30 600 820] '
                        f'/CropBox [10 20 580 800] /Rotate {rotation} /UserUnit {unit} '
                        f'/Resources << /Font << /F1 3 0 R >> >> /Contents {page+1} 0 R >>').encode())
        content = b'BT /F1 24 Tf 40 740 Td (Hello World) Tj 0 -40 Td (Second line) Tj 0 -40 Td <80818220832084> Tj ET\nBT /F1 10 Tf 0 -10 Td (HIDDEN) Tj ET'
        objects.append(f'<< /Length {len(content)} >>\nstream\n'.encode()+content+b'\nendstream')
objects[1] = f'<< /Type /Pages /Count 8 /Kids [{" ".join(f"{p} 0 R" for p in pages)}] >>'.encode()
pdf = bytearray(b'%PDF-1.7\n')
offsets = [0]
for i, obj in enumerate(objects, 1):
    offsets.append(len(pdf))
    pdf += f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
xref = len(pdf)
pdf += f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode()
for offset in offsets[1:]:
    pdf += f'{offset:010} 00000 n \n'.encode()
pdf += f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
(target/'text.pdf').write_bytes(pdf)
(target/'text-manifest.json').write_text(json.dumps({'source':'original MIT, no embedded font', 'generator':'tools/make-text-fixture.py', 'sha256':hashlib.sha256(pdf).hexdigest(), 'rotations':[0,90,180,270], 'user_units':[1,2], 'unicode':'Chinese, supplementary, combining, ligature; ToUnicode tests mapping, not glyph appearance'},indent=2),encoding='utf-8',newline='\n')

# A real text page above the extraction cap, for worker-side limit validation.
content = b'\n'.join(b'BT /F1 10 Tf 40 '+str(y).encode()+b' Td ('+b'A'*30000+b') Tj ET' for y in (740,700,660))
limit_objects = [b'<< /Type /Catalog /Pages 2 0 R >>',
    b'<< /Type /Pages /Count 1 /Kids [3 0 R] >>',
    b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>',
    b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    f'<< /Length {len(content)} >>\nstream\n'.encode()+content+b'\nendstream']
limit = bytearray(b'%PDF-1.7\n')
offsets = [0]
for i, obj in enumerate(limit_objects, 1):
    offsets.append(len(limit))
    limit += f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
xref = len(limit)
limit += b'xref\n0 6\n0000000000 65535 f \n'
for offset in offsets[1:]:
    limit += f'{offset:010} 00000 n \n'.encode()
limit += f'trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
(target/'text-limit.pdf').write_bytes(limit)

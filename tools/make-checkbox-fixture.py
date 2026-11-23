"""Original MIT tick-box fixture: printed boxes, distractors and a rotated page. Stdlib only.

Page 1 (595 x 842 pt, French text layer):
  - six 8 pt hollow boxes, red and black, each followed by a word (true boxes);
  - a filled square, a box with a printed cross, a 30 pt frame, a table of touching
    cells and a 3 pt square (all must be ignored).
Page 2: the same first row on a /Rotate 90 page, to check coordinate conversion.
"""
from pathlib import Path

objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'', b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>']
pages = []
FONT = 3


def add_page(box, content, extra=b''):
    page = len(objects) + 1
    pages.append(page)
    objects.append(b'<< /Type /Page /Parent 2 0 R /MediaBox ' + box + b' /Contents ' + str(page + 1).encode()
                   + b' 0 R /Resources << /Font << /F1 ' + str(FONT).encode() + b' 0 R >> >> ' + extra + b' >>')
    objects.append(f'<< /Length {len(content)} >>\nstream\n'.encode() + content + b'\nendstream')


def text(x, y, words, size=11):
    return f'BT /F1 {size} Tf {x} {y} Td ({words}) Tj ET\n'.encode('latin-1')


def box(x, y, side=8, colour=(0.85, 0.15, 0.2), width=0.8):
    r, g, b = colour
    return f'q {r} {g} {b} RG {width} w {x} {y} {side} {side} re S Q\n'.encode()


ROW = [(72, 'le sport'), (180, 'la lecture'), (300, 'la danse')]


def first_row(y):
    out = b''
    for x, words in ROW:
        out += box(x, y) + text(x + 14, y, words)
    return out


page1 = text(72, 780, 'Cochez la case : vous aimez le sport et la lecture ?', 13)
page1 += first_row(740)
for x, words in [(72, 'oui'), (180, 'non'), (300, 'peut-etre')]:
    page1 += box(x, 700, colour=(0, 0, 0)) + text(x + 14, 700, words)
# Distractors.
page1 += b'q 0 0 0 rg 72 640 8 8 re f Q\n' + text(86, 640, 'plein')
page1 += box(180, 640, colour=(0, 0, 0)) + b'q 0 G 0.8 w 181 641 m 187 647 l S 187 641 m 181 647 l S Q\n' + text(194, 640, 'deja coche')
page1 += box(300, 620, side=30, colour=(0, 0, 0)) + text(340, 630, 'cadre')
page1 += b'q 0 G 0.8 w 72 560 8 8 re S 80 560 8 8 re S 88 560 8 8 re S 72 568 8 8 re S 80 568 8 8 re S 88 568 8 8 re S Q\n'
page1 += box(180, 564, side=3, colour=(0, 0, 0)) + text(190, 562, 'petit')
add_page(b'[0 0 595 842]', page1)
add_page(b'[0 0 842 595]', first_row(500), b'/Rotate 90')

objects[1] = f'<< /Type /Pages /Count {len(pages)} /Kids [{" ".join(f"{p} 0 R" for p in pages)}] >>'.encode()
pdf = bytearray(b'%PDF-1.7\n')
offsets = [0]
for index, obj in enumerate(objects, 1):
    offsets.append(len(pdf))
    pdf += f'{index} 0 obj\n'.encode() + obj + b'\nendobj\n'
xref = len(pdf)
pdf += f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode()
for offset in offsets[1:]:
    pdf += f'{offset:010} 00000 n \n'.encode()
pdf += f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
(Path(__file__).resolve().parents[1] / 'tests/fixtures/check-boxes.pdf').write_bytes(pdf)

"""Original MIT answer-rule fixtures: vector, CropBox, rotation, raster. Stdlib only."""
from pathlib import Path
import zlib

objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'']
pages = []


def add_page(box, content, extra=b'', image=None):
    page = len(objects) + 1
    pages.append(page)
    resources = b''
    if image is not None:
        resources = f'/Resources << /XObject << /Im {page+2} 0 R >> >>'.encode()
    objects.append(b'<< /Type /Page /Parent 2 0 R /MediaBox '+box+b' /Contents '+str(page+1).encode()+b' 0 R '+resources+b' '+extra+b' >>')
    objects.append(f'<< /Length {len(content)} >>\nstream\n'.encode()+content+b'\nendstream')
    if image is not None:
        data = zlib.compress(image)
        objects.append(f'<< /Type /XObject /Subtype /Image /Width 600 /Height 800 /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length {len(data)} >>\nstream\n'.encode()+data+b'\nendstream')


content = b'0 G 1 w 100 500 m 280 500 l S 100 400 m 340 400 l S\n100 600 m 280 600 l S 110 604 150 10 re f\n350 300 200 100 re S'
add_page(b'[0 0 600 800]', content)
add_page(b'[0 0 640 840]', b'q 1 0 0 1 10 20 cm '+content+b' Q', b'/CropBox [10 20 610 820]')
add_page(b'[0 0 800 600]', b'0 G 1 w 500 100 m 500 280 l S 400 100 m 400 340 l S', b'/Rotate 90')
pixels = bytearray([245]) * (600 * 800)
for y in (300, 400):
    for x in range(100, 280 if y == 300 else 340):
        pixels[y*600+x] = 35
add_page(b'[0 0 600 800]', b'q 600 0 0 800 0 0 cm /Im Do Q', image=bytes(pixels))
objects[1] = f'<< /Type /Pages /Count {len(pages)} /Kids [{" ".join(f"{p} 0 R" for p in pages)}] >>'.encode()
pdf = bytearray(b'%PDF-1.7\n')
offsets = [0]
for index, obj in enumerate(objects, 1):
    offsets.append(len(pdf))
    pdf += f'{index} 0 obj\n'.encode()+obj+b'\nendobj\n'
xref = len(pdf)
pdf += f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode()
for offset in offsets[1:]:
    pdf += f'{offset:010} 00000 n \n'.encode()
pdf += f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
(Path(__file__).resolve().parents[1]/'tests/fixtures/answer-lines.pdf').write_bytes(pdf)

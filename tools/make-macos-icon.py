"""Render the LinguaPDF mark (same geometry as tools/make-app-icon.ps1) as a macOS .icns.

Uses the macOS icon grid (1024 canvas, 824 body, continuous-corner approximation) and the locked
PyMuPDF vector drawing to rasterize every iconset size, then the system `iconutil`. Output is committed:
assets/icons/linguapdf.icns. Run: .tools/venv/bin/python tools/make-macos-icon.py
"""
from pathlib import Path
import subprocess
import tempfile

import fitz  # PyMuPDF, pinned in tools/requirements-phase0.lock.txt

ROOT = Path(__file__).resolve().parents[1]
PINE = '#2c7a68'


def draw(page, canvas=1024.0):
    inset = canvas * 100 / 1024
    box = canvas - 2 * inset
    u = box / 24.0
    white = fitz.utils.getColor('white')
    shape = page.new_shape()
    # radius is relative to the shorter side: 0.2237 of the body, the macOS app icon corner.
    shape.draw_rect(fitz.Rect(inset, inset, inset + box, inset + box), radius=0.2237)
    shape.finish(fill=tuple(int(PINE[i:i + 2], 16) / 255 for i in (1, 3, 5)), color=None)
    for row in (9.5, 15.5):
        def p(x, y):
            return fitz.Point(inset + x * u, inset + y * u)
        shape.draw_bezier(p(4.5, row), p(7.5, row - 3.2), p(9.5, row - 3.2), p(12, row))
        shape.draw_bezier(p(12, row), p(14.5, row + 3.2), p(16.5, row + 3.2), p(19.5, row))
        shape.finish(color=white, width=2.0 * u * 1.15, lineCap=1, closePath=False)
    shape.commit()


def main():
    document = fitz.open()
    page = document.new_page(width=1024, height=1024)
    draw(page)
    with tempfile.TemporaryDirectory() as work:
        iconset = Path(work) / 'linguapdf.iconset'
        iconset.mkdir()
        for points in (16, 32, 128, 256, 512):
            for scale, suffix in ((1, ''), (2, '@2x')):
                pixels = points * scale
                zoom = pixels / page.rect.width
                pixmap = page.get_pixmap(matrix=fitz.Matrix(zoom, zoom), alpha=True)
                pixmap.save(iconset / f'icon_{points}x{points}{suffix}.png')
        output = ROOT / 'assets/icons/linguapdf.icns'
        subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(output)], check=True)
    print(f'Wrote {output.relative_to(ROOT)}')


if __name__ == '__main__':
    main()

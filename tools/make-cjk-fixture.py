"""Original PDF with subset-embedded, locked OFL Noto font for selection verification.
Development-only PyMuPDF; no product dependency. See third_party/notices/Noto-OFL.txt.
"""
from pathlib import Path
import hashlib
import json
import fitz

root = Path(__file__).resolve().parents[1]
doc = fitz.open()
page = doc.new_page(width=600, height=800)
page.insert_font(fontname='NotoFixture', fontfile=str(root/'.deps/fonts/NotoSansSC-Regular.ttf'))
page.insert_text((40,80), '中文练习 Bonjour', fontname='NotoFixture', fontsize=24)
doc.subset_fonts()
doc.set_metadata({'title':'Original Chinese selection fixture', 'author':'Langlern contributors',
                  'subject':'Embedded Noto Sans SC subset under SIL OFL 1.1; see third_party/notices/Noto-OFL.txt'})
doc.save(root/'tests/fixtures/chinese.pdf', garbage=4, deflate=True, no_new_id=True)
(root/'tests/fixtures/chinese-manifest.json').write_text(json.dumps({
    'generator':'tools/make-cjk-fixture.py', 'source':'original text; embedded locked Noto Sans SC subset',
    'font_license':'third_party/notices/Noto-OFL.txt',
    'sha256':hashlib.sha256((root/'tests/fixtures/chinese.pdf').read_bytes()).hexdigest(),
    'text':'中文练习 Bonjour'},ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')

from pathlib import Path
import fitz
root = Path(__file__).resolve().parents[1] / 'tests/fixtures'
doc = fitz.open(root / 'geometry.pdf')
doc.save(root / 'password.pdf', encryption=fitz.PDF_ENCRYPT_AES_256, owner_pw='owner', user_pw='reader')
scan = fitz.open()
page = scan.new_page(width=300, height=400)
pix = doc[0].get_pixmap(matrix=fitz.Matrix(1, 1))
page.insert_image(page.rect, pixmap=pix)
scan.save(root / 'scan.pdf')
print('Original geometry rasterized to image-only scan.pdf; password.pdf requires reader. Test assets only.')

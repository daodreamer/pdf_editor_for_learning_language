"""Build the bundled answer fonts in assets/fonts from locked upstream files.

Both answer fonts get the vertical metrics of the existing Noto Sans SC face, so answer
line heights and baselines (tuned against printed rules) stay the same on every platform.
That makes them Modified Versions under the SIL OFL, which must not use a Reserved Font
Name, so they are renamed; copyright, manufacturer, designer and licence records are kept.

- Andika (SIL; reserved names "Andika", "SIL") -> "Langlern Sans", glyphs unchanged.
- LXGW WenKai (reserved names "LXGW", "霞鹜", ...) -> "Langlern Kai", subset to GB2312 plus
  punctuation (characters outside it fall back to Noto Sans SC).

Run with the project venv (fontTools): .tools/venv/bin/python tools/make-answer-fonts.py
"""
from pathlib import Path
import hashlib
import json
import shutil
import urllib.request

from fontTools import subset
from fontTools.ttLib import TTFont

root = Path(__file__).resolve().parents[1]
lock = json.loads((root / "third_party/dependencies.lock.json").read_text())["answerFonts"]
cache = root / ".deps/answer-fonts"
out = root / "assets/fonts"


def fetch(entry):
    target = cache / entry["file"]
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        urllib.request.urlretrieve(entry["url"], target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    if digest != entry["sha256"]:
        raise RuntimeError(f"SHA256 mismatch: {entry['file']} {digest}")
    return target


def gb2312():
    chars = set()
    for b1 in range(0xA1, 0xF8):
        for b2 in range(0xA1, 0xFF):
            try:
                chars.add(bytes([b1, b2]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return chars


RESERVED = ("LXGW", "霞鹜", "霞鶩", "落霞孤", "Andika", "SIL")


def match_noto_metrics(font, noto):
    # Same ascent/descent/gap per em as Noto in hhea, OS/2 typo and win metrics.
    scale = font["head"].unitsPerEm / noto["head"].unitsPerEm
    for table, fields in (("hhea", ("ascent", "descent", "lineGap")),
                          ("OS/2", ("sTypoAscender", "sTypoDescender", "sTypoLineGap", "usWinAscent", "usWinDescent"))):
        for field in fields:
            setattr(font[table], field, round(getattr(noto[table], field) * scale))
    font["OS/2"].fsSelection = (font["OS/2"].fsSelection & ~(1 << 7)) | (noto["OS/2"].fsSelection & (1 << 7))


def rename(font, family, postscript, weight, unique):
    # Only name records are replaced; copyright, manufacturer, designer, URLs and
    # licence keep the credit to the original authors.
    style = "Bold" if weight == "Bold" else "Regular"
    legacy = family if weight in ("Regular", "Bold") else f"{family} {weight}"
    names = {1: legacy, 2: style, 3: f"{postscript}-{weight};{unique}", 4: f"{family} {weight}",
             6: f"{postscript}-{weight}", 16: family, 17: weight}
    table = font["name"]
    for record in list(table.names):
        if record.nameID in names:
            table.removeNames(nameID=record.nameID)
    for nameID, value in names.items():
        if nameID in (16, 17) and weight in ("Regular", "Bold"):
            continue
        table.setName(value, nameID, 3, 1, 0x409)
        table.setName(value, nameID, 1, 0, 0)
    for record in table.names:
        text = record.toUnicode()
        # Name fields only; copyright, manufacturer, designer, URLs and licence keep the credit.
        if any(reserved in text for reserved in RESERVED) and record.nameID in (1, 2, 3, 4, 6, 16, 17, 18, 21, 22, 25):
            raise RuntimeError(f"reserved name left in record {record.nameID}: {text}")


out.mkdir(parents=True, exist_ok=True)
noto = TTFont(out / "NotoSansSC-Regular.ttf")
for entry in lock:
    source = fetch(entry)
    if entry["kind"] == "licence":
        shutil.copyfile(source, out / entry["output"])
    else:
        font = TTFont(source, recalcTimestamp=False)  # reproducible output
        if entry["kind"] == "subset":
            options = subset.Options()
            options.layout_features = ["*"]
            options.hinting = False
            options.name_IDs = ["*"]
            options.name_languages = ["*"]
            subsetter = subset.Subsetter(options)
            unicodes = [*range(0x20, 0x7F), *range(0xA0, 0x100), *range(0x2000, 0x2070), *range(0x3000, 0x3040), *range(0xFF00, 0xFFF0)]
            subsetter.populate(text="".join(sorted(gb2312())), unicodes=unicodes)
            subsetter.subset(font)
        match_noto_metrics(font, noto)
        rename(font, entry["family"], entry["postscript"], entry["weight"], entry["unique"])
        font.save(out / entry["output"])
    data = (out / entry["output"]).read_bytes()
    print(entry["output"], len(data), hashlib.sha256(data).hexdigest())

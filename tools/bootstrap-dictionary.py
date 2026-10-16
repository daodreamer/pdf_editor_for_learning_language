"""Prepare the pinned offline ECDICT database. Network is used only by this developer tool."""
import csv
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
lock = json.loads((ROOT / 'third_party/dictionary.lock.json').read_text(encoding='utf-8'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', type=Path, default=ROOT / '.deps/dictionary')
folder = parser.parse_args().output_dir.resolve()
folder.mkdir(parents=True, exist_ok=True)
for name in ('LICENSE', 'ecdict.csv'):
    expected = next(item for item in lock['files'] if item['name'] == name)
    path = folder / name
    if not path.exists():
        urllib.request.urlretrieve(f"https://raw.githubusercontent.com/skywind3000/ECDICT/{lock['commit']}/{name}", path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected['sha256']:
        raise RuntimeError(f'Pinned dictionary source checksum mismatch: {name}')
database = folder / 'ecdict.sqlite'
if database.exists():
    with closing(sqlite3.connect(f'{database.as_uri()}?mode=ro', uri=True)) as connection:
        if connection.execute('SELECT COUNT(*) FROM words').fetchone()[0] != 770611:
            raise RuntimeError('Existing database has an unexpected word count; retained for inspection')
else:
    pending = folder / 'ecdict.sqlite.pending'
    if pending.exists():
        raise FileExistsError('Incomplete dictionary database exists; inspect it before rebuilding')
    with closing(sqlite3.connect(pending)) as connection, connection:
        connection.execute('CREATE TABLE words(word TEXT PRIMARY KEY COLLATE NOCASE, phonetic TEXT, meaning TEXT, definition TEXT)')
        with (folder / 'ecdict.csv').open(encoding='utf-8-sig', newline='') as source:
            connection.executemany('INSERT OR REPLACE INTO words VALUES(?,?,?,?)', (
                (row['word'], row['phonetic'], row['translation'].replace('\\n', '\n'), row['definition'].replace('\\n', '\n'))
                for row in csv.DictReader(source)))
        if connection.execute('SELECT COUNT(*) FROM words').fetchone()[0] != 770611:
            raise RuntimeError('Dictionary conversion count mismatch')
    pending.rename(database)
print(f'Offline dictionary ready: {database}. Reconfigure the application to copy it into the build.')

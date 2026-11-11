"""Build the offline French dictionary (lemma, English glosses, inflections, conjugation).

Source: Wiktextract JSONL of French entries from English Wiktionary (kaikki.org).
Network is used only by this developer tool; the application reads the SQLite result offline.

kaikki.org publishes the file under a fixed name and refreshes it roughly weekly, so the
lock file pins the SHA-256 that was reviewed. A different upstream file is rejected unless
--accept-source is given, which records the new hash and extraction metadata in the lock.
--source builds from a local JSONL (for example the test fixture) and marks the database
as unverified sample data; the build never copies such a database into the application.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / 'third_party/dictionary-fr.lock.json'
SCHEMA_VERSION = 2
DATABASE_NAME = 'fr-wiktionary.sqlite'
MAX_SENSES = 12
# Usage labels worth showing in front of a gloss; grammatical tags stay in the analysis line.
QUALIFIERS = ('informal', 'colloquial', 'slang', 'vulgar', 'formal', 'literary', 'figuratively',
              'archaic', 'obsolete', 'dated', 'rare', 'transitive', 'intransitive', 'pronominal',
              'reflexive', 'impersonal', 'Quebec', 'Belgium', 'Switzerland')
# Conjugation rows that are table metadata, compound tenses or spelling variants, not forms.
NOISE_TAGS = {'abbreviation', 'initialism', 'acronym', 'clipping', 'misspelling', 'nonstandard'}
SKIP_CONJUGATION_TAGS = {'table-tags', 'inflection-template', 'multiword-construction', 'alternative', 'reflexive'}


def key(text):
    return text.replace('’', "'").lower()


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def gloss_lines(senses):
    lines = []
    for sense in senses:
        glosses = sense.get('glosses') or []
        if not glosses or sense.get('form_of') or sense.get('alt_of') or 'form-of' in (sense.get('tags') or []):
            continue
        labels = [tag for tag in sense.get('tags') or [] if tag in QUALIFIERS]
        text = glosses[-1].strip()
        line = f"({', '.join(labels)}) {text}" if labels else text
        if line not in lines:
            lines.append(line)
    return lines


def build(source, database, metadata):
    words, forms, seen_forms, tagsets = [], [], set(), {}

    def add_form(form, lemma, pos, tags):
        tagset = tagsets.setdefault(tags, len(tagsets))
        item = (key(form), lemma, pos, tagset)
        if item not in seen_forms:
            seen_forms.add(item)
            forms.append(item)

    with source.open(encoding='utf-8') as stream:
        for raw in stream:
            entry = json.loads(raw)
            if entry.get('lang_code') != 'fr' or not entry.get('word'):
                continue
            word, pos = entry['word'], entry.get('pos') or ''
            senses = entry.get('senses') or []
            for sense in senses:
                for target in (sense.get('form_of') or []) + (sense.get('alt_of') or []):
                    lemma = (target.get('word') or '').strip()
                    if not lemma or lemma == word or ' ' in lemma and ' ' not in word:
                        continue
                    raw_tags = set(sense.get('tags') or [])
                    if raw_tags & NOISE_TAGS:
                        break  # abbreviations point at unrelated words (eu -> États-Unis)
                    tags = ' '.join(sorted(raw_tags - {'form-of', 'alt-of'}))
                    if sense.get('alt_of') and not sense.get('form_of'):
                        tags = (tags + ' alternative').strip()
                    add_form(word, lemma, pos, tags)
                    break  # later form_of targets are English cross-references (e.g. yeux -> œil, eyes)
            lines = gloss_lines(senses)
            if not lines:
                continue
            ipa = next((s['ipa'] for s in entry.get('sounds') or [] if s.get('ipa')), '')
            sense_tags = {t for s in senses for t in s.get('tags') or []}
            gender = ' '.join(g for g in ('masculine', 'feminine') if g in sense_tags)
            auxiliaries, participle = [], ''
            if pos == 'verb':
                for form in entry.get('forms') or []:
                    tags = set(form.get('tags') or [])
                    text = (form.get('form') or '').strip()
                    if form.get('source') != 'conjugation' or not text:
                        continue
                    # Rows look like "avoir + past participle", or a bare "avoir" next to
                    # "être + past participle" for verbs taking both (passer, sortir).
                    # Pronominal variants ("s'en être + ...") belong to another headword.
                    if tags == {'infinitive', 'multiword-construction'}:
                        first = text.split(' ')[0]
                        if first in ('avoir', 'être') and first not in auxiliaries:
                            auxiliaries.append(first)
                    if tags & SKIP_CONJUGATION_TAGS or ' ' in text:
                        continue
                    # The verb's own table is authoritative for compound tenses; other entries may
                    # also claim to be its participle (été is listed under aller for "j'ai été").
                    if tags == {'participle', 'past'} and not participle:
                        participle = text
                    # The conjugation table is read back from forms by lemma, so the infinitive stays too.
                    add_form(text, word, pos, ' '.join(sorted(tags)))
            aux = '/'.join(sorted(auxiliaries))  # "avoir", "être" or "avoir/être"
            words.append((key(word), word, pos, gender, ipa, aux, participle, len(lines), '\n'.join(lines[:MAX_SENSES])))
    with closing(sqlite3.connect(database)) as connection, connection:
        connection.executescript('''
            CREATE TABLE meta(name TEXT PRIMARY KEY, value TEXT);
            CREATE TABLE entries(key TEXT, word TEXT, pos TEXT, gender TEXT, ipa TEXT, aux TEXT, participle TEXT, senses INTEGER, glosses TEXT);
            CREATE TABLE tagsets(id INTEGER PRIMARY KEY, tags TEXT);
            CREATE TABLE forms(key TEXT, lemma TEXT, pos TEXT, tagset INTEGER);''')
        connection.executemany('INSERT INTO entries VALUES(?,?,?,?,?,?,?,?,?)', words)
        connection.executemany('INSERT INTO forms VALUES(?,?,?,?)', forms)
        connection.executemany('INSERT INTO tagsets VALUES(?,?)', [(i, t) for t, i in tagsets.items()])
        connection.executescript('''
            CREATE INDEX entries_key ON entries(key);
            CREATE INDEX forms_key ON forms(key);
            CREATE INDEX forms_lemma ON forms(lemma);''')
        connection.executemany('INSERT INTO meta VALUES(?,?)', [(k, str(v)) for k, v in {**metadata, 'schema': SCHEMA_VERSION}.items()])
    with closing(sqlite3.connect(database)) as connection:
        connection.execute('VACUUM')
    return len(words), len(forms), len(tagsets)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output-dir', type=Path, default=ROOT / '.deps/dictionary-fr')
    parser.add_argument('--source', type=Path, help='Build from a local JSONL instead of the pinned download (marked as sample).')
    parser.add_argument('--accept-source', action='store_true', help='Accept a refreshed upstream file and update the lock.')
    args = parser.parse_args()
    folder = args.output_dir.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    lock = json.loads(LOCK_PATH.read_text(encoding='utf-8'))
    if args.source:
        if folder == (ROOT / '.deps/dictionary-fr').resolve():
            raise RuntimeError('Sample builds need an explicit --output-dir; .deps/dictionary-fr is copied into the application')
        source = args.source.resolve()
        metadata = {'source': source.name, 'sha256': sha256(source), 'sample': 1}
    else:
        source = folder / lock['file']
        if not source.exists():
            pending = source.with_suffix('.part')
            print(f"Downloading {lock['url']} ({lock['bytes'] / 1e6:.0f} MB)...")
            urllib.request.urlretrieve(lock['url'], pending)
            pending.rename(source)
        digest = sha256(source)
        if digest != lock['sha256']:
            if not args.accept_source:
                raise RuntimeError(f'Upstream French data changed (sha256 {digest}); review it and rerun with --accept-source, '
                                   f'or delete {source} to download again')
            lock.update(sha256=digest, bytes=source.stat().st_size)
            LOCK_PATH.write_text(json.dumps(lock, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print('Lock updated; record the new extraction date from the kaikki.org page in the lock and DEPENDENCIES.md.')
        metadata = {'source': lock['url'], 'sha256': digest, 'extracted': lock['extracted'], 'sample': 0}
    database = folder / DATABASE_NAME
    pending = folder / (DATABASE_NAME + '.pending')
    pending.unlink(missing_ok=True)
    counts = build(source, pending, metadata)
    database.unlink(missing_ok=True)
    pending.rename(database)
    print(f'French dictionary ready: {database} ({database.stat().st_size / 1e6:.1f} MB; '
          f'{counts[0]} entries, {counts[1]} inflections, {counts[2]} tag sets). Reconfigure to copy it into the build.')


if __name__ == '__main__':
    main()

"""Prepare pinned CPU Pocket TTS / French / Estelle artifacts (no runtime downloads)."""
import hashlib
import json
from pathlib import Path
import struct
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(url, path, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and digest(path) == expected:
        return
    pending = path.with_suffix(path.suffix + '.pending')
    with urllib.request.urlopen(url, timeout=120) as source, pending.open('wb') as output:
        while chunk := source.read(4 * 1024 * 1024):
            output.write(chunk)
    if digest(pending) != expected:
        raise ValueError('Pocket dependency checksum mismatch: ' + path.name)
    pending.replace(path)


def sentencepiece_model(tokenizer):
    """Encode the fixed JSON Unigram vocabulary as SentencePiece ModelProto.

    Wire fields follow the pinned SentencePiece src/sentencepiece_model.proto.
    Preserve IDs/scores/byte fallback and identity normalization; never use NFKC.
    Model control-token strings are treated literally when reading user text.
    """
    assert tokenizer['model']['type'] == 'Unigram'
    assert tokenizer['model']['byte_fallback'] and tokenizer['model']['unk_id'] == 0
    assert tokenizer['normalizer'] == {'type':'Sequence', 'normalizers':[{'type':'Prepend','prepend':'▁'}]}
    assert tokenizer['pre_tokenizer'] == {'type':'Metaspace','replacement':'▁','prepend_scheme':'always','split':True}
    vocabulary = tokenizer['model']['vocab']
    assert len(vocabulary) == 4000

    def varint(value):
        result = bytearray()
        while value > 127:
            result.append((value & 127) | 128)
            value >>= 7
        result.append(value)
        return bytes(result)

    def integer(field, value):
        return varint(field << 3) + varint(value)

    def data(field, value):
        return varint((field << 3) | 2) + varint(len(value)) + value

    pieces = []
    for index, (piece, score) in enumerate(vocabulary):
        kind = 2 if index == 0 else 3 if index < 4 else 6 if piece.startswith('<0x') and len(piece) == 6 else 1
        pieces.append(data(1, data(1, piece.encode()) + varint(2 << 3 | 5) + struct.pack('<f', score) + integer(3, kind)))
    trainer = integer(3, 1) + integer(4, 4000) + integer(35, 1)
    trainer += integer(40, 0) + integer(41, 1) + integer(42, 2) + integer(43, 3)
    normalizer = data(1, b'identity') + integer(3, 1) + integer(4, 0) + integer(5, 1)
    return b''.join(pieces) + data(2, trainer) + data(3, normalizer)


def main():
    lock = json.loads((ROOT / 'third_party/pocket.lock.json').read_text(encoding='utf-8'))
    sources = ROOT / '.deps/pocket-source'
    for item in lock['sources']:
        name = item['name']
        filename = {'onnxruntime':'onnxruntime.zip', 'sentencepiece':'sentencepiece.tar.gz',
                    'soundtouch':'soundtouch-2.3.3.tar.gz'}[name]
        path = sources / filename
        download(item['url'], path, item['sha256'])
        directory = {'onnxruntime':'onnxruntime-win-x64-1.23.2', 'sentencepiece':'sentencepiece-0.2.1',
                     'soundtouch':'soundtouch'}[name]
        if not (sources / directory).exists():
            if name == 'onnxruntime':
                with zipfile.ZipFile(path) as archive:
                    archive.extractall(sources)
            else:
                with tarfile.open(path) as archive:
                    archive.extractall(sources, filter='data')
    for item in lock['files']:
        path = ROOT / '.deps/pocket-estelle' / item['file']
        if 'derived_from' in item:
            source = ROOT / '.deps/pocket-estelle' / item['derived_from']
            path.write_bytes(sentencepiece_model(json.loads(source.read_text(encoding='utf-8'))))
            if path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
                raise ValueError('Derived tokenizer checksum mismatch')
        else:
            download(item['url'], path, item['sha256'])
    for name, expected in lock['notices'].items():
        if digest(ROOT / 'third_party/notices/pocket' / name) != expected:
            raise ValueError('Pocket notice checksum mismatch: ' + name)
    resource = sources / 'soundtouch/source/SoundTouchDLL/SoundTouchDLL.rc'
    resource.write_bytes(resource.read_bytes().replace(b'afxres.h', b'winresrc.h'))
    print('Pinned CPU Pocket TTS / French / Estelle models and sources are ready')


if __name__ == '__main__':
    main()

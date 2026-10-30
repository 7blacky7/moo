"""Decode exact token bytes from pinned P4 tokenizer artifacts.

This verifies structural invariants needed for byte alignment. The native Moo
loader must independently validate the same immutable blob; in particular
this table does not implement the silben pattern parser or BPE encoding.
"""
import hashlib
import struct


def _u32(blob, offset):
    if offset < 0 or offset + 4 > len(blob):
        raise ValueError("truncated tokenizer")
    return struct.unpack_from("<I", blob, offset)[0]


class TokenTable:
    def __init__(self, kind, blob, expected_sha256):
        if not isinstance(blob, bytes) or not 0 < len(blob) <= 16 * 1024 * 1024:
            raise ValueError("invalid tokenizer size")
        if hashlib.sha256(blob).hexdigest() != expected_sha256:
            raise ValueError("tokenizer hash mismatch")
        self.digest = expected_sha256
        self.blob = blob
        table = [bytes([i]) for i in range(256)]
        if kind == "char":
            if (len(blob) < 52 or blob[:8] != b"MOOCF001" or _u32(blob, 8) != 1
                    or _u32(blob, 12) != 0 or hashlib.sha256(blob[:-32]).digest() != blob[-32:]):
                raise ValueError("invalid char artifact")
            count = _u32(blob, 16)
            if not 256 <= count <= 65536 or len(blob) != 20 + 4 * (count - 256) + 32:
                raise ValueError("invalid char vocabulary")
            for i in range(count - 256):
                cp = _u32(blob, 20 + i * 4)
                if cp < 128 or cp > 0x10ffff or 0xd800 <= cp <= 0xdfff:
                    raise ValueError("invalid Unicode scalar")
                table.append(chr(cp).encode("utf-8"))
            if len(set(table)) != len(table):
                raise ValueError("duplicate char token")
        elif kind == "syllable":
            if (len(blob) < 56 or blob[:8] != b"MOOSIL01" or _u32(blob, 8) != 1
                    or _u32(blob, 20) != 0 or hashlib.sha256(blob[:-32]).digest() != blob[-32:]):
                raise ValueError("invalid syllable artifact")
            pn, count = _u32(blob, 12), _u32(blob, 16)
            if not 0 < pn <= 1024 * 1024 or count > 65536 - 256 or 24 + pn > len(blob) - 32:
                raise ValueError("invalid syllable header")
            pos = 24 + pn
            for _ in range(count):
                length = _u32(blob, pos)
                pos += 4
                if not 0 < length <= 512 or pos + length > len(blob) - 32:
                    raise ValueError("invalid syllable token")
                part = blob[pos:pos + length]
                if b"\0" in part:
                    raise ValueError("NUL syllable")
                part.decode("utf-8", errors="strict")
                table.append(part)
                pos += length
            if pos != len(blob) - 32 or len(set(table[256:])) != count:
                raise ValueError("duplicate or trailing syllable data")
        elif kind == "bpe":
            if len(blob) < 32 or blob[:8] != b"MOOBPE01" or _u32(blob, 8) != 1 or _u32(blob, 12) != 0:
                raise ValueError("invalid BPE artifact")
            vocab, merges, special = _u32(blob, 16), _u32(blob, 20), _u32(blob, 24)
            if not 256 <= vocab <= 65536 or special != 0 or vocab != 256 + merges or _u32(blob, 28) != 0:
                raise ValueError("unsupported BPE configuration")
            if 32 + 8 * merges > len(blob):
                raise ValueError("truncated BPE merges")
            pairs = []
            for i in range(merges):
                a, b = _u32(blob, 32 + 8 * i), _u32(blob, 36 + 8 * i)
                if a >= 256 + i or b >= 256 + i:
                    raise ValueError("invalid BPE merge")
                pairs.append((a, b))
            pos = 32 + 8 * merges
            table = []
            for _ in range(vocab):
                length = _u32(blob, pos)
                pos += 4
                if length == 0 or pos + length > len(blob):
                    raise ValueError("truncated BPE token")
                table.append(blob[pos:pos + length])
                pos += length
            if pos != len(blob) or table[:256] != [bytes([i]) for i in range(256)]:
                raise ValueError("invalid BPE base/trailing data")
            for i, (a, b) in enumerate(pairs):
                if table[256 + i] != table[a] + table[b]:
                    raise ValueError("BPE merge does not match vocabulary")
        else:
            raise ValueError("unknown tokenizer kind")
        self.tokens = tuple(table)

    def __call__(self, blob, token):
        if not isinstance(blob, bytes) or (blob is not self.blob and hashlib.sha256(blob).hexdigest() != self.digest):
            raise ValueError("tokenizer changed")
        if type(token) is not int or not 0 <= token < len(self.tokens):
            raise ValueError("invalid token ID")
        return self.tokens[token]

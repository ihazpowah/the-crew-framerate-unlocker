"""Offline PE comparison for executable compatibility investigation (never executes input)."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


class PE:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        self.nt = self.u32(0x3c)
        self.optional = self.nt + 24
        self.base = struct.unpack_from('<Q', self.data, self.optional + 24)[0]
        self.image_size = self.u32(self.optional + 56)
        self.sections = []
        start = self.optional + struct.unpack_from('<H', self.data, self.nt + 20)[0]
        for i in range(struct.unpack_from('<H', self.data, self.nt + 6)[0]):
            off = start + 40 * i
            name = self.data[off:off+8].rstrip(b'\0').decode(errors='replace')
            size, rva, rawsize, raw = struct.unpack_from('<IIII', self.data, off+8)
            self.sections.append(dict(name=name, rva=rva, size=size, rawsize=rawsize, raw=raw))

    def u32(self, off):
        return struct.unpack_from('<I', self.data, off)[0]

    def read(self, rva, size):
        for s in self.sections:
            delta = rva - s['rva']
            if 0 <= delta and delta + size <= s['rawsize']:
                return self.data[s['raw']+delta:s['raw']+delta+size]
        return None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('left')
    p.add_argument('right')
    args = p.parse_args()
    a, b = PE(args.left), PE(args.right)
    for pe in (a, b):
        print(json.dumps(dict(sha256=hashlib.sha256(pe.data).hexdigest(), image_size=hex(pe.image_size),
                              base=hex(pe.base), sections=pe.sections)))
    for s in a.sections:
        other = b.read(s['rva'], s['rawsize'])
        original = a.read(s['rva'], s['rawsize'])
        if other is not None:
            print(s['name'], 'same bytes at same RVA:', sum(x==y for x,y in zip(original,other)), '/', len(original))


if __name__ == '__main__':
    main()

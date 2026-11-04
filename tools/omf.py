#!/usr/bin/env python3
"""Minimal OMF (Intel/Microsoft .OBJ) reader used to pull embedded data out of
the Second Reality object files.

usage:
  omf.py info  FILE.OBJ               list segments and public symbols
  omf.py dump  FILE.OBJ SEG OUT.bin   write the raw bytes of segment SEG (name or index)
  omf.py pub   FILE.OBJ SYM OUT.bin [LEN]
                                       write bytes of the segment holding SYM,
                                       starting at the symbol (optionally LEN bytes)
"""
import struct, sys


def read_index(b, o):
    v = b[o]
    if v & 0x80:
        return ((v & 0x7F) << 8) | b[o + 1], o + 2
    return v, o + 1


class OMF:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        self.lnames = ['']
        self.segs = []      # dicts: name, length, data(bytearray)
        self.pubs = []      # (name, segindex(1-based), offset)
        self.groups = []
        self.parse()

    def parse(self):
        d = self.data
        o = 0
        last = None  # (seg, offset) of last LEDATA, for FIXUPP context (unused)
        while o + 3 <= len(d):
            typ = d[o]
            ln = struct.unpack_from('<H', d, o + 1)[0]
            body = d[o + 3:o + 3 + ln - 1]
            o += 3 + ln
            big = typ & 1
            t = typ & 0xFE
            if t == 0x96:  # LNAMES
                p = 0
                while p < len(body):
                    n = body[p]
                    self.lnames.append(body[p + 1:p + 1 + n].decode('latin1'))
                    p += 1 + n
            elif t == 0x98:  # SEGDEF
                attr = body[0]
                p = 1
                if (attr >> 5) == 0:  # absolute
                    p += 3
                if big:
                    length = struct.unpack_from('<I', body, p)[0]; p += 4
                else:
                    length = struct.unpack_from('<H', body, p)[0]; p += 2
                if attr & 2:  # big bit: 64K
                    length = 0x10000
                ni, p = read_index(body, p)
                ci, p = read_index(body, p)
                self.segs.append({'name': self.lnames[ni], 'class': self.lnames[ci],
                                  'length': length, 'data': bytearray(length)})
            elif t == 0x9A:  # GRPDEF
                pass
            elif t == 0x90:  # PUBDEF
                p = 0
                gi, p = read_index(body, p)
                si, p = read_index(body, p)
                if si == 0:
                    p += 2
                while p < len(body):
                    n = body[p]
                    name = body[p + 1:p + 1 + n].decode('latin1'); p += 1 + n
                    if big:
                        off = struct.unpack_from('<I', body, p)[0]; p += 4
                    else:
                        off = struct.unpack_from('<H', body, p)[0]; p += 2
                    _, p = read_index(body, p)  # type index
                    self.pubs.append((name, si, off))
            elif t == 0xA0:  # LEDATA
                p = 0
                si, p = read_index(body, p)
                if big:
                    off = struct.unpack_from('<I', body, p)[0]; p += 4
                else:
                    off = struct.unpack_from('<H', body, p)[0]; p += 2
                chunk = body[p:]
                seg = self.segs[si - 1]
                end = off + len(chunk)
                if end > len(seg['data']):
                    seg['data'].extend(bytes(end - len(seg['data'])))
                seg['data'][off:end] = chunk
                last = (si, off)
            elif t == 0xA2:  # LIDATA
                p = 0
                si, p = read_index(body, p)
                if big:
                    off = struct.unpack_from('<I', body, p)[0]; p += 4
                else:
                    off = struct.unpack_from('<H', body, p)[0]; p += 2
                out = bytearray()
                while p < len(body):
                    blk, p = self.expand(body, p, big)
                    out += blk
                seg = self.segs[si - 1]
                end = off + len(out)
                if end > len(seg['data']):
                    seg['data'].extend(bytes(end - len(seg['data'])))
                seg['data'][off:end] = out
            elif t == 0x8A:  # MODEND
                break

    def expand(self, b, p, big):
        if big:
            rep = struct.unpack_from('<I', b, p)[0]; p += 4
        else:
            rep = struct.unpack_from('<H', b, p)[0]; p += 2
        cnt = struct.unpack_from('<H', b, p)[0]; p += 2
        if cnt == 0:
            n = b[p]; p += 1
            blk = bytes(b[p:p + n]); p += n
        else:
            blk = bytearray()
            for _ in range(cnt):
                sub, p = self.expand(b, p, big)
                blk += sub
        return bytes(blk) * rep, p

    def seg_by(self, key):
        try:
            i = int(key)
            return self.segs[i - 1]
        except ValueError:
            for s in self.segs:
                if s['name'].lower() == key.lower():
                    return s
        raise KeyError(key)


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    m = OMF(path)
    if cmd == 'info':
        for i, s in enumerate(m.segs, 1):
            print('seg %d %-16s %-10s len=%d' % (i, s['name'], s['class'], s['length']))
        for n, si, off in m.pubs:
            print('pub %-20s seg %d off %d' % (n, si, off))
    elif cmd == 'dump':
        s = m.seg_by(sys.argv[3])
        open(sys.argv[4], 'wb').write(bytes(s['data'][:s['length']]))
    elif cmd == 'pub':
        for n, si, off in m.pubs:
            if n == sys.argv[3]:
                s = m.segs[si - 1]
                data = bytes(s['data'][off:s['length']])
                if len(sys.argv) > 5:
                    data = data[:int(sys.argv[5])]
                open(sys.argv[4], 'wb').write(data)
                return
        sys.exit('symbol not found')


if __name__ == '__main__':
    main()

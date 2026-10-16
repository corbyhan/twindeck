"""Prints exception code/address and module RVA from a minidump (no debugger needed)."""
import struct, sys
d = open(sys.argv[1], 'rb').read()
sig, ver, nstreams, rva = struct.unpack_from('<4sIII', d, 0)
streams = {}
for i in range(nstreams):
    t, size, off = struct.unpack_from('<III', d, rva + i * 12)
    streams[t] = (off, size)
mods = []
if 4 in streams:
    off, _ = streams[4]
    n = struct.unpack_from('<I', d, off)[0]
    for i in range(n):
        base, size = struct.unpack_from('<QI', d, off + 4 + i * 108)
        name_rva = struct.unpack_from('<I', d, off + 4 + i * 108 + 20)[0]
        ln = struct.unpack_from('<I', d, name_rva)[0]
        name = d[name_rva + 4:name_rva + 4 + ln].decode('utf-16-le')
        mods.append((base, size, name))
if 6 in streams:
    off, _ = streams[6]
    tid = struct.unpack_from('<I', d, off)[0]
    code, flags, rec, addr = struct.unpack_from('<IIQQ', d, off + 8)
    nparams = struct.unpack_from('<I', d, off + 8 + 24)[0]
    params = struct.unpack_from('<2Q', d, off + 8 + 32)
    print('exception code 0x%08x at 0x%x params %s' % (code, addr, [hex(p) for p in params]))
    for b, s, n in mods:
        if b <= addr < b + s:
            print('module %s base 0x%x rva 0x%x' % (n, b, addr - b))
    # context -> rsp for stack scan
    ctx_size, ctx_rva = struct.unpack_from('<II', d, off + 8 + 152)
    rip = struct.unpack_from('<Q', d, ctx_rva + 0xF8)[0]
    rsp = struct.unpack_from('<Q', d, ctx_rva + 0x98)[0]
    print('rip 0x%x rsp 0x%x' % (rip, rsp))
    # scan stack memory for return addresses inside TwinDeck.exe
    exe = [m for m in mods if m[2].lower().endswith('twindeck.exe')][0]
    if 5 in streams:
        off5, _ = streams[5]
        n = struct.unpack_from('<I', d, off5)[0]
        for i in range(n):
            start, dsize, drva = struct.unpack_from('<QII', d, off5 + 4 + i * 16)
            if start <= rsp < start + dsize:
                o = rsp - start
                hits = []
                for p in range(o, min(dsize, o + 0x4000), 8):
                    v = struct.unpack_from('<Q', d, drva + p)[0]
                    if exe[0] <= v < exe[0] + exe[1]:
                        hits.append(v - exe[0])
                print('stack exe rvas:', ' '.join(hex(h) for h in hits[:40]))

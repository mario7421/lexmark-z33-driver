#!/usr/bin/env bash
# tests/run_tests.sh - functional + security test suite for rastertolxz33.
#
# Golden hashes pin the exact printer-protocol output for a fixed input
# raster; if you intentionally change the band encoding, regenerate the
# expected hashes with:
#     sha256sum < out
# and update GOLDEN_* below.
set -euo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d /tmp/lxz33-tests.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0
ok()  { echo "PASS: $*"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $*"; FAIL=$((FAIL+1)); }

FILTER="$ROOT/src/rastertolxz33"

# Golden hash of the printer stream produced for tools/mkraster's
# synthetic page (600 dpi, A4, 1 bpp K, black bar + dashes + 50% band).
GOLDEN_PATTERN=d21d2123f045d4725030432c19af2fd4f636fed694d517c80a05a4f9b1c702ef

# ---------------------------------------------------------------- build ----
echo "== build =="
if make -C "$ROOT" >/dev/null 2>&1; then ok "build filter"; else bad "build filter"; exit 1; fi

if gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
       -DZ33_PREAMBLE_FILENAME="\"$ROOT/src/z33-preamble.bin\"" \
       -o "$TMP/rasterto-asan" "$ROOT/src/rastertolxz33.c" \
       -lcupsimage -lcups 2>/dev/null; then
    ok "build ASan+UBSan variant"
else
    bad "build ASan+UBSan variant"; exit 1
fi

# ------------------------------------------------------------ static QA ----
echo "== static checks =="
if command -v cupstestppd >/dev/null && cupstestppd -q "$ROOT/src/lxz33.ppd" 2>/dev/null; then
    ok "PPD validates (cupstestppd)"
elif command -v cupstestppd >/dev/null; then
    bad "PPD validates (cupstestppd)"
else
    echo "SKIP: cupstestppd not installed"
fi
bash -n "$ROOT/install.sh" && bash -n "$ROOT/uninstall.sh" && ok "shell scripts parse"

# Queue-name extraction self-test (ships inside install.sh itself).
if "$ROOT/install.sh" --selftest-queue-name > "$TMP/selftest.out" 2>&1; then
    ok "install.sh queue-name extraction ($(grep -c '^PASS' "$TMP/selftest.out") fixtures)"
else
    bad "install.sh queue-name extraction"; sed 's/^/    /' "$TMP/selftest.out"
fi

# --------------------------------------------------------- golden tests ----
echo "== golden output tests =="
"$ROOT/tools/mkraster" > "$TMP/pattern-v3.ras"
"$FILTER" t 1 u t 1 "" < "$TMP/pattern-v3.ras" > "$TMP/out-v3.z33" 2>/dev/null

H3="$(sha256sum < "$TMP/out-v3.z33" | cut -d' ' -f1)"
if [ "$H3" = "$GOLDEN_PATTERN" ]; then
    ok "pattern page output matches golden hash"
else
    bad "pattern page golden hash mismatch (got $H3)"
fi

# The stream must open with the Z33 preamble and be non-trivial.
if [ "$(head -c 4 "$TMP/out-v3.z33" | od -An -tx1 | tr -d ' \n')" = "1b2f8100" ]; then
    ok "output starts with Z33 preamble"
else
    bad "output preamble missing"
fi
if [ "$(wc -c < "$TMP/out-v3.z33")" -gt 70000 ]; then
    ok "output size plausible"
else
    bad "output suspiciously small"
fi

# ---------------------------------------------------- format equivalence ----
echo "== v1/v2/v3 equivalence =="
python3 "$ROOT/tests/convert_raster.py" v1 < "$TMP/pattern-v3.ras" > "$TMP/pattern-v1.ras"
python3 "$ROOT/tests/convert_raster.py" v2 < "$TMP/pattern-v3.ras" > "$TMP/pattern-v2.ras"
"$FILTER" t 1 u t 1 "" < "$TMP/pattern-v1.ras" > "$TMP/out-v1.z33" 2>/dev/null
"$FILTER" t 1 u t 1 "" < "$TMP/pattern-v2.ras" > "$TMP/out-v2.z33" 2>/dev/null
if cmp -s "$TMP/out-v1.z33" "$TMP/out-v3.z33" && cmp -s "$TMP/out-v2.z33" "$TMP/out-v3.z33"; then
    ok "v1, v2 and v3 inputs produce byte-identical printer output"
else
    bad "raster version outputs differ"
fi

# -------------------------------------------------------- hostile corpus ----
echo "== hostile input corpus (sanitizers + validation) =="
python3 "$ROOT/tools/gen-hostile-raster.py" "$TMP/hostile" >/dev/null

# Cases that must be rejected by the header validator (rc != 0, no output).
MUST_REJECT="02_trunc_hdr 03_bad_sync 04_attack_wide 05_width0 06_bpl_mismatch \
07_res0 08_k_bpp8 09_cmyk_bpp1 11_width_over 13_v1_attack 14_huge_height"
# Cases that must process without a memory-safety finding.
MUST_ACCEPT="01_valid_blank 10_trunc_pixels 12_cmyk_valid"

for name in $MUST_REJECT $MUST_ACCEPT; do
    f="$TMP/hostile/$name.ras"
    "$TMP/rasterto-asan" t 1 u t 1 "" < "$f" > "$TMP/h-out.bin" 2> "$TMP/h-err.txt" || true
    if grep -qE 'AddressSanitizer|UBSan|runtime error|LeakSanitizer' "$TMP/h-err.txt"; then
        bad "$name: sanitizer finding"
        continue
    fi
    case " $MUST_REJECT " in
        *" $name "*)
            # expect rejection: non-zero exit and no printer output
            rc=0; "$TMP/rasterto-asan" t 1 u t 1 "" < "$f" >/dev/null 2>&1 || rc=$?
            if [ "$rc" -ne 0 ] && [ ! -s "$TMP/h-out.bin" ]; then
                ok "$name: rejected cleanly"
            else
                bad "$name: not rejected (rc=$rc, out=$(wc -c < "$TMP/h-out.bin"))"
            fi
            ;;
        *)
            ok "$name: processed without sanitizer findings"
            ;;
    esac
done

# ------------------------------------------------------------------ end ----
echo
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]

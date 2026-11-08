#!/usr/bin/env bash
# install.sh - build, install and set up the Lexmark Z23/Z33 CUPS driver.
#
# Usage:            ./install.sh          (auto-elevates with sudo)
#                   ./install.sh --test   (also print a test page)
set -euo pipefail

# Keep tool output parseable regardless of the user's locale.
export LC_ALL=C

SRCDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TESTPAGE=0
[ "${1:-}" = "--test" ] && TESTPAGE=1

say()  { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m==>\033[0m %s\n' "$*" >&2; }

# Extract the queue name from an "lpstat -v" line ("device for NAME: URI"
# under LC_ALL=C). Pure string ops: no regex metachar pitfalls with URIs,
# and names containing spaces are kept intact.
extract_queue_name() { # $1 = line, $2 = uri
    printf '%s\n' "$1" | LPURI="$2" awk '{
        uri = ENVIRON["LPURI"]; line = $0;
        if (uri != "" && substr(line, length(line) - length(uri) + 1) == uri)
            line = substr(line, 1, length(line) - length(uri));
        sub(/:[[:space:]]*$/, "", line);
        if (line ~ /^device for /) { sub(/^device for /, "", line); print line; }
        else { n = split(line, w, /[[:space:]]+/); print w[n]; }
    }'
}

# Self-test for the queue-name extraction (used by tests/run_tests.sh).
if [ "${1:-}" = "--selftest-queue-name" ]; then
    t() { # t <expected> <line> <uri>
        got="$(extract_queue_name "$2" "$3")"
        if [ "$got" = "$1" ]; then echo "PASS: name extraction ('$2')"
        else echo "FAIL: name extraction ('$2') -> '$got', expected '$1'"; FAILED=1; fi
    }
    URI=usb://Lexmark/Z23-Z33
    t "Lexmark-Z23-Z33"   "device for Lexmark-Z23-Z33: $URI"            "$URI"
    t "Lexmark-Z23-Z33"   "device for Lexmark-Z23-Z33: $URI?serial=AB12" "$URI?serial=AB12"
    t "My Z33 Printer"    "device for My Z33 Printer: $URI"             "$URI"
    # A line for a different printer never reaches the extractor in the
    # real flow (grep -F filters it); if it ever did, the raw output is
    # the remainder, and the name guard in the main flow rejects it
    # (contains '/'), falling back to 'Z33'.
    t "Other: usb://Something/Else" "device for Other: usb://Something/Else" "$URI"
    t ""                  ""                                            "$URI"
    [ -z "${FAILED:-}" ] || exit 1
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

# ---------------------------------------------------------------- deps ----
if ! command -v cups-config >/dev/null 2>&1 || ! command -v gcc >/dev/null 2>&1; then
    say "Installing build dependencies..."
    if   command -v apt-get >/dev/null 2>&1; then
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential libcups2-dev libcupsimage2-dev ghostscript
    elif command -v dnf >/dev/null 2>&1; then
        dnf install -y gcc make cups cups-devel ghostscript
    elif command -v pacman >/dev/null 2>&1; then
        pacman -Sy --noconfirm base-devel libcups ghostscript
    elif command -v zypper >/dev/null 2>&1; then
        zypper --non-interactive install gcc make cups cups-devel ghostscript
    else
        err "Could not detect a package manager."
        err "Please install manually: gcc, make, CUPS development files (cups-config), ghostscript."
        exit 1
    fi
fi

command -v cups-config >/dev/null 2>&1 || { err "cups-config not found; install the CUPS development package."; exit 1; }

# Wait briefly for the scheduler to become ready (it may be restarting).
for _ in $(seq 1 15); do
    if lpstat -r 2>/dev/null | grep -qi running; then break; fi
    sleep 1
done
lpstat -r 2>/dev/null | grep -qi running || { err "CUPS is not running."; exit 1; }

# --------------------------------------------------------------- build ----
say "Building rastertolxz33..."
make -C "$SRCDIR" >/dev/null

say "Installing ($(cups-config --serverbin), $(cups-config --datadir))..."
make -C "$SRCDIR" install >/dev/null

# ------------------------------------------------------------- no restart ----
# CUPS resolves the filter from the queue's PPD at job time and execs it
# fresh, so no scheduler restart or reload is needed (and on
# socket-activated systems like Debian, SIGHUP terminates cupsd).

# --------------------------------------------------------- queue setup ----
say "Looking for a Lexmark Z23/Z33 on USB..."
URI="$(lpinfo -v 2>/dev/null | awk '$2 ~ /^usb:\/\/Lexmark\/Z[23][0-9]/ {print $2; exit}' || true)"

if [ -z "$URI" ]; then
    say "No Lexmark Z23/Z33 found on USB; driver installed."
    say "Connect the printer and run this script again to configure the queue."
else
    # Reuse an existing queue on this URI if present, else create 'Z33'.
    NAME="$(extract_queue_name "$(lpstat -v 2>/dev/null | grep -F "$URI" | head -1)" "$URI")"
    # Guard against garbage names: CUPS queue names must be short, printable,
    # and free of URI metacharacters.
    if [ -z "$NAME" ] || [ "${#NAME}" -gt 127 ] || printf '%s' "$NAME" | grep -qE '[/?#]'; then
        NAME=Z33
    fi
    say "Configuring queue '$NAME' on $URI..."
    LPADMIN=lpadmin
    command -v lpadmin >/dev/null 2>&1 || LPADMIN=/usr/sbin/lpadmin
    "$LPADMIN" -p "$NAME" -E -v "$URI" -P "$SRCDIR/src/lxz33.ppd" 2>/dev/null \
        || "$LPADMIN" -p "$NAME" -E -v "$URI" -P "$SRCDIR/src/lxz33.ppd"
    cupsenable  "$NAME" 2>/dev/null || /usr/sbin/cupsenable  "$NAME" || true
    cupsaccept  "$NAME" 2>/dev/null || /usr/sbin/cupsaccept  "$NAME" || true
    say "Queue '$NAME' is ready."
    say "Default mode: black, A4. Print with:  lp -d $NAME file.pdf"

    if [ "$TESTPAGE" = 1 ]; then
        say "Printing test page..."
        lp -d "$NAME" -o ColorModel=Black /usr/share/cups/data/testprint 2>/dev/null \
            || lp -d "$NAME" /usr/share/cups/data/default-testpage.pdf
        say "Test page queued."
    fi
fi

say "Done."

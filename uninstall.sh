#!/usr/bin/env bash
# uninstall.sh - remove the rastertolxz33 driver files (queues are kept).
set -euo pipefail

export LC_ALL=C
SRCDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

make -C "$SRCDIR" uninstall

# No scheduler restart/reload: CUPS execs filters fresh from the queue's
# PPD on every job, and on socket-activated systems (e.g. Debian) SIGHUP
# terminates cupsd rather than reloading it.
echo "Driver files removed. Existing printer queues were left in place;"
echo "reconfigure them (e.g. via the CUPS web UI) if you want to use"
echo "a different driver afterwards."

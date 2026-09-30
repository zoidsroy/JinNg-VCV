#!/usr/bin/env bash
# Builds the plugin, runs the unit tests and installs into Rack's user folder.
# Run from an MSYS2 MINGW64 shell: scripts/build.sh
set -u
cd "$(dirname "$0")/.."

log=$(mktemp)
if ! make -j4 >"$log" 2>&1; then
	grep -E 'error' -A4 "$log" | head -60
	rm -f "$log"
	echo "BUILD FAILED"
	exit 1
fi
grep -E 'warning' -A4 "$log" | grep -v vla-extension | head -40
rm -f "$log"

make test 2>&1 | grep -v '^ok' | tail -30
[ "${PIPESTATUS[0]}" -eq 0 ] || { echo "TESTS FAILED"; exit 1; }

make install 2>&1 | tail -1

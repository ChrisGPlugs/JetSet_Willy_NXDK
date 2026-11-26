#!/usr/bin/env bash
# Compare the translated code with the original Z80 code, pass by pass.
# Needs ./difftest (make -f Makefile.host difftest) and the tape in
# "Data files/JetSetWilly.tzx" (or JSW_TAPE).
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail=0
check() {
    local script=$1 passes=$2
    ./difftest ref  "$script" "$passes" > "$tmp/ref.txt"  || true
    ./difftest port "$script" "$passes" > "$tmp/port.txt" || true
    local first
    first=$(diff "$tmp/ref.txt" "$tmp/port.txt" | sed -n 2p || true)
    local rooms games
    rooms=$(awk 'NF>3{print $4}' "$tmp/ref.txt" | sort -un | wc -l)
    games=$(awk 'NF>3 && $5!=p {c++} {p=$5} END{print c+0}' "$tmp/ref.txt")
    if [ -z "$first" ] && [ "$(wc -l < "$tmp/ref.txt")" -gt "$passes" ]; then
        [ -n "${JSW_ORIG:-}" ] && script="$script(orig)"
        printf 'PASS  %-8s %6d passes, %2d rooms, %3d lives-count changes (RAM + T-states identical)\n' "$script" "$passes" "$rooms" "$games"
    else
        echo "FAIL  $script: $(tail -1 "$tmp/ref.txt") | first difference: $first"
        fail=1
    fi
}
check idle 3000
check finale 4000
for s in 1 2 3 4 5; do check "play:$s" "${PASSES:-20000}"; done
JSW_ORIG=1 check play:9 10000
# Code card kept: two wrong entries, then the game resets the machine (JP 0).
./difftest ref codes 1 > "$tmp/ref.txt" || true
./difftest port codes 1 > "$tmp/port.txt" || true
if cmp -s "$tmp/ref.txt" "$tmp/port.txt" && grep -q RESET "$tmp/ref.txt"; then echo "PASS  codes    code card, two wrong codes, machine reset (RAM identical)"; else echo "FAIL  codes"; fail=1; fi
exit $fail

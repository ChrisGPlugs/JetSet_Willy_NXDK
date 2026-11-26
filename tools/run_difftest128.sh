#!/usr/bin/env bash
# Compare the translated JSW128 (HL9) engine with the original, pass by pass:
# all RAM (every bank), T-states, and the sequence of AY register writes.
# Needs ./difftest128 and ./difftest128p (make -f Makefile.host difftest128)
# and the tape in "Data files/JetSetWilly128.tzx" (or JSW128_TAPE).
set -euo pipefail
cd "$(dirname "$0")/.."
export JSW128_TAPE="${JSW128_TAPE:-$PWD/Data files/JetSetWilly128.tzx}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
check() {
    local script=$1 passes=$2 f="$tmp/${1//:/_}"
    ./difftest128  ref  "$script" "$passes" > "$f.ref"  || true
    ./difftest128p port "$script" "$passes" > "$f.port" || true
    local rooms
    rooms=$(awk '{print $6}' "$f.ref" | sort -u | wc -l)
    if cmp -s "$f.ref" "$f.port" && [ "$(wc -l < "$f.ref")" -gt "$passes" ]; then
        printf 'PASS  %-8s %6d passes, %3d rooms (RAM, T-states, AY writes identical)\n' "$script" "$passes" "$rooms"
    else
        echo "FAIL  $script: $(diff "$f.ref" "$f.port" | sed -n 2p) / port: $(tail -1 "$f.port")"
        return 1
    fi
}
pids=()
for s in title idle finale play:1 play:2 play:3 walk:1 walk:2 keys:1 keys:2; do
    check "$s" "${PASSES:-20000}" > "$tmp/res_${s//:/_}" 2>&1 & pids+=($!)
done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
cat "$tmp"/res_*
exit $fail

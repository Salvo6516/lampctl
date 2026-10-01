#!/bin/sh
# Offline tests: parse known descriptors with `lampctl decode`. No hardware needed.
set -eu
bin=${1:-./lampctl}
dir=$(dirname "$0")/descriptors
fail=0
check() { if ! printf '%s\n' "$2" | grep -Eq "$3"; then echo "FAIL: $1 (expected /$3/)"; fail=1; else echo "ok:   $1"; fi; }

out=$($bin decode "$(cat "$dir/ite-048d-c693-lamparray.hex")")
check "finds 6 LampArray reports"   "$out" '6 LampArray report\(s\)'
check "Attributes is 0x01, 23 B"    "$out" 'report 0x01 feature Attributes +23 bytes'
check "AttrResponse is 0x03, 29 B"  "$out" 'report 0x03 feature AttrResponse +29 bytes'
check "MultiUpdate is 0x04, 51 B"   "$out" 'report 0x04 feature MultiUpdate +51 bytes'
check "RangeUpdate is 0x05, 10 B"   "$out" 'report 0x05 feature RangeUpdate +10 bytes'
check "Control is 0x06, 2 B"        "$out" 'report 0x06 feature Control +2 bytes'

out=$($bin decode "$(cat "$dir/ite-048d-c693-vendor.hex")")
check "vendor interface is ignored" "$out" '0 LampArray report\(s\)'
exit $fail

#!/usr/bin/env bash
# Behavioral check for the SeaHorn InstCombine Avoid* flags.
#
# For each test, run it through `seaopt -sea-instcombine`, assert the
# verification-friendly invariant, run the LLVM verifier on the output, and
# print the stock `opt -instcombine` result for contrast.
#
# Usage:
#   SEAOPT=/path/to/seaopt OPT=opt-14 ./run.sh        # LLVM 14 baseline
#   SEAOPT=./build/bin/seaopt OPT=opt-15 ./run.sh     # LLVM 15 (dev15)
set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SEAOPT="${SEAOPT:-seaopt}"
OPT="${OPT:-opt}"

# test : "required regex" : "forbidden regex"
declare -A REQUIRE=(
  [avoidbv_urem_pow2]='urem i32 %x, 8'
  [avoidbv_add_disjoint]='= add.*i32 %a, %b'
  [avoidunsignedicmp_slt]='icmp slt'
  [avoidaliasing_phi_load]='phi i32 \['
)
declare -A FORBID=(
  [avoidbv_urem_pow2]='= and'
  [avoidbv_add_disjoint]='= or'
  [avoidunsignedicmp_slt]='icmp ult'
  [avoidaliasing_phi_load]='phi (i32\*|ptr)'
)
# extra required line for the aliasing test (both loads survive)
declare -A REQUIRE2=(
  [avoidaliasing_phi_load]='load i32, i32\* %q'
)

fail=0
for t in avoidbv_urem_pow2 avoidbv_add_disjoint avoidunsignedicmp_slt avoidaliasing_phi_load; do
  f="$DIR/$t.ll"
  out="$("$SEAOPT" -sea-instcombine -S < "$f" 2>/dev/null)"
  ok=1
  grep -Eq "${REQUIRE[$t]}"  <<<"$out" || ok=0
  if [[ -n "${REQUIRE2[$t]:-}" ]]; then grep -Eq "${REQUIRE2[$t]}" <<<"$out" || ok=0; fi
  ! grep -Eq "${FORBID[$t]}" <<<"$out" || ok=0
  # verify the produced IR is well-formed (catches opaque-ptr malformations)
  vrfy=$("$OPT" -S -passes=verify <<<"$out" 2>&1 >/dev/null) || ok=0
  if [[ $ok -eq 1 ]]; then
    echo "PASS  $t"
  else
    fail=1
    echo "FAIL  $t"
    echo "  --- seaopt output ---"; sed 's/^/    /' <<<"$out"
    echo "  --- stock $OPT -instcombine (contrast) ---"
    "$OPT" -S -passes=instcombine < "$f" 2>/dev/null | sed 's/^/    /'
    [[ -n "$vrfy" ]] && { echo "  --- verifier ---"; sed 's/^/    /' <<<"$vrfy"; }
  fi
done
exit $fail

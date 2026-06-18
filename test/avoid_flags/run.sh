#!/usr/bin/env bash
# Behavioral checks for the SeaHorn-specific transform customizations.
#
# For each test, run it through `seaopt` with the relevant legacy pass, assert
# the verification-friendly invariant, run the LLVM verifier on the output, and
# print the stock `opt` result for contrast on failure.
#
# seaopt keeps the legacy pass manager, so the sea passes are invoked as
# `-sea-*`. The stock contrast and the verifier use the new-PM `-passes=` form
# so they work on both LLVM 14 and LLVM 15 (LLVM 15 dropped the legacy `opt`
# pass flags).
#
# Usage:
#   SEAOPT=/path/to/seaopt OPT=opt-14 ./run.sh        # LLVM 14 baseline
#   SEAOPT=./build/bin/seaopt OPT=opt-15 ./run.sh     # LLVM 15 (dev15)
set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SEAOPT="${SEAOPT:-seaopt}"
OPT="${OPT:-opt}"

TESTS=(
  avoidbv_urem_pow2
  avoidbv_add_disjoint
  avoidunsignedicmp_slt
  avoidaliasing_phi_load
  loopunroll_ignore_disable
)
# seaopt legacy pass to run for each test
declare -A PASS=(
  [avoidbv_urem_pow2]=sea-instcombine
  [avoidbv_add_disjoint]=sea-instcombine
  [avoidunsignedicmp_slt]=sea-instcombine
  [avoidaliasing_phi_load]=sea-instcombine
  [loopunroll_ignore_disable]=sea-loop-unroll
)
# required / forbidden regexes in the seaopt output
declare -A REQUIRE=(
  [avoidbv_urem_pow2]='urem i32 %x, 8'
  [avoidbv_add_disjoint]='= add.*i32 %a, %b'
  [avoidunsignedicmp_slt]='icmp slt'
  [avoidaliasing_phi_load]='phi i32 \['
  [loopunroll_ignore_disable]='store i32 3,'
)
declare -A FORBID=(
  [avoidbv_urem_pow2]='= and'
  [avoidbv_add_disjoint]='= or'
  [avoidunsignedicmp_slt]='icmp ult'
  [avoidaliasing_phi_load]='phi (i32\*|ptr)'
  [loopunroll_ignore_disable]='= phi'
)
declare -A REQUIRE2=(
  [avoidaliasing_phi_load]='load i32, i32\* %q'
)

fail=0
for t in "${TESTS[@]}"; do
  f="$DIR/$t.ll"
  p="${PASS[$t]}"
  out="$("$SEAOPT" "-$p" -S < "$f" 2>/dev/null)"
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
    echo "  --- seaopt -$p output ---"; sed 's/^/    /' <<<"$out"
    echo "  --- stock $OPT -passes=${p#sea-} (contrast) ---"
    "$OPT" -S -passes="${p#sea-}" < "$f" 2>/dev/null | sed 's/^/    /'
    [[ -n "$vrfy" ]] && { echo "  --- verifier ---"; sed 's/^/    /' <<<"$vrfy"; }
  fi
done
exit $fail

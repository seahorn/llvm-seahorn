# SeaHorn transform behavioral corpus

These tests pin the *intended* behavior of the SeaHorn-specific passes. Each
test is built so that a **stock** LLVM pass performs a transformation SeaHorn
wants to suppress (or skips one SeaHorn wants to force), and asserts that the
corresponding `seaopt -sea-*` pass does the SeaHorn thing instead. The stock
`opt` result is the oracle.

## InstCombine Avoid* flags

In `seaopt` the flags are hardcoded **on**
(`AvoidBv = AvoidUnsignedICmp = AvoidIntToPtr = AvoidAliasing = true`, see
`InstructionCombining.cpp` ~line 4542), so there is no run-time off switch.
Validated against a `seaopt` built from `dev14` on LLVM 14:

| File | Flag | stock instcombine | `seaopt -sea-instcombine` keeps |
|------|------|-------------------|----------------|
| `avoidbv_urem_pow2.ll`      | AvoidBv            | `and i32 %x, 7`              | `urem i32 %x, 8` |
| `avoidbv_add_disjoint.ll`   | AvoidBv            | `or i32 %a, %b`             | `add nuw nsw i32 %a, %b` |
| `avoidunsignedicmp_slt.ll`  | AvoidUnsignedICmp | `icmp ult`                  | `icmp slt` |
| `avoidaliasing_phi_load.ll` | AvoidAliasing     | `phi i32*` + single `load`  | two `load`s + `phi i32` |

`avoidaliasing_phi_load.ll` doubles as the opaque-pointer canary: the suppressed
transform (`FoldPHIArgLoadIntoPHI`) builds a pointer-typed phi + a new load, so
it exercises the pointer-construction paths LLVM 15's opaque pointers change.
On LLVM 15 the merged form is `phi ptr`; the test forbids both `phi i32*` and
`phi ptr`, so it works unchanged on either toolchain.

`AvoidIntToPtr` is intentionally **not** covered: in dev14 the flag is set and
has an accessor (`seaAvoidIntToPtr()`) but is never read anywhere. Confirm or
restore its gating during the port, then add a test here.

## Loop passes

| File | Pass | stock | `seaopt -sea-loop-unroll` |
|------|------|-------|----------------|
| `loopunroll_ignore_disable.ll` | LoopUnroll | respects `llvm.loop.unroll.disable` (loop kept) | ignores it → fully unrolled |

SeaHorn's LoopUnroll deliberately ignores the `llvm.loop.unroll.disable`
metadata (it only logs "Forcing Loop Unroll despite disable metadata" where
stock bails). Validated on LLVM 14.

Two other loop customizations are **not** behaviorally unit-tested here, by
design:

- **IndVarSimplify disequality avoidance** (`sea-indvars`): SeaHorn preserves the
  `slt`/`ult` exit predicate where stock LFTR would emit `icmp ne`. On LLVM 14,
  stock `-indvars` is too conservative to perform that LFTR rewrite on ordinary
  integer-counter loops -- even through the full `-O2` pipeline -- so any test
  asserting the divergence would pass *vacuously*. Re-evaluate on LLVM 15.
- **LoopRotate aggressiveness** (`sea-loop-rotate`): not registered as a
  standalone legacy pass in `seaopt` (it is wired only through the pass-manager
  pipeline), so it cannot be driven in isolation; and at `-O2` a large header
  either folds below the stock threshold or blocks rotation, so the aggressive
  threshold has no observable effect to assert on LLVM 14.

## Pipeline test

`pipeline_o2.ll` runs the full SeaHorn `-O2` pipeline (`PassManagerBuilder`),
which engages sea-instcombine *and* the sea loop passes:

- **Behavioral**: `urem`-by-pow2 survives `seaopt -O2` but stock `opt -O2` folds
  it to `and` -- proving the pipeline uses `createSeaInstructionCombiningPass`
  rather than stock InstCombine (a wiring regression the single-pass tests miss).
- **Smoke/verify**: a loop function drives sea-loop-rotate / sea-indvars /
  sea-loop-unroll under `-O2`, and the verifier RUN line asserts the output is
  well-formed. Since those passes have no assertable behavioral divergence on
  LLVM 14 (see above), this is their coverage -- it catches crashes / malformed
  IR (e.g. opaque-pointer breakage during the port), not a specific rewrite.

## Running

The corpus runs under `llvm-lit` (this is what CI uses). Tool paths come from
the environment, so the same tests run against any build:

```sh
# LLVM 14 baseline (proven green)
SEAOPT=/path/to/seaopt OPT=opt-14 FILECHECK=FileCheck-14 lit -v test/sea_transforms

# LLVM 15 (dev15 build under test)
SEAOPT=./build/bin/seaopt OPT=opt-15 FILECHECK=FileCheck-15 lit -v test/sea_transforms
```

Each test (see its `RUN:` lines) does three things:
1. asserts the SeaHorn invariant on the `seaopt` output (`CHECK` / `CHECK-NOT`);
2. runs the LLVM verifier on that output (catches malformed/opaque-pointer IR);
3. asserts that stock `opt` actually **diverges** — the `STOCK:` lines, matched
   against `opt -passes=<pass>`, do what SeaHorn avoids/forces.

Step 3 is the non-vacuity guard: if a future LLVM makes stock behave like
SeaHorn, the `STOCK:` match fails (`expected string not found`) and the test
fails loudly instead of passing silently.

The `.ll` files are the single source of truth: the `RUN:`/`CHECK:`/`STOCK:`
lines fully define each test, with no separate runner to keep in sync.

## Build note

Ubuntu's `llvm-N` packages reference `libPolly.a` in their CMake exports but do
not ship it. The `Extensions` link component pulls Polly in, so building
`seaopt` against a distro LLVM may require dropping `Extensions` from
`tools/opt/CMakeLists.txt` (`LLVM_LINK_COMPONENTS`).

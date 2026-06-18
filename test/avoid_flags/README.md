# Avoid-flag behavioral corpus

These tests pin the *intended* behavior of the SeaHorn-specific InstCombine
modifications. In `seaopt` the flags are hardcoded **on**
(`AvoidBv = AvoidUnsignedICmp = AvoidIntToPtr = AvoidAliasing = true`, see
`InstructionCombining.cpp` ~line 4542), so there is no run-time off switch.

Each test is built so that **stock** instcombine performs a transformation that
SeaHorn wants to suppress, and asserts that `seaopt -sea-instcombine` leaves the
verification-friendly form instead. The stock `opt` result is the oracle.

All four were validated against a `seaopt` built from `dev14` on LLVM 14:

| File | Flag | stock `opt -instcombine` | `seaopt` keeps |
|------|------|--------------------------|----------------|
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

## Running

```sh
# LLVM 14 baseline (proven green)
SEAOPT=/path/to/seaopt OPT=opt-14 ./run.sh

# LLVM 15 (dev15 build under test)
SEAOPT=./build/bin/seaopt OPT=opt-15 ./run.sh
```

`run.sh` checks the invariant, runs the LLVM verifier on each output (catching
malformed IR such as opaque-pointer mistakes), and prints the stock contrast for
any failing case. Exit code is non-zero if any test fails.

The `.ll` files also carry lit-style `RUN:`/`CHECK:` lines, so they can be driven
by `llvm-lit` + `FileCheck` once a lit config is added.

## Build notes (this machine)

Reproducing the `seaopt` used to validate these tests:

- Use a real CMake (the `~/.local/bin/cmake` shim is broken); e.g.
  `~/cmake-3.31.7-linux-x86_64/bin/cmake`.
- Ubuntu's `llvm-14` package references `libPolly.a` in its CMake exports but
  does not ship it. The `Extensions` link component drags Polly in, so for a
  local `seaopt` build drop `Extensions` from `tools/opt/CMakeLists.txt`
  (`LLVM_LINK_COMPONENTS`). Expect the same with `llvm-15-dev`.

; AvoidBv: "X urem Y -> X & (Y-1)" when Y is a power of two is suppressed.
; Gated at InstCombineMulDivRem.cpp (`if (!AvoidBv && isKnownToBeAPowerOfTwo ...)`).
;
; Validated on LLVM 14:
;   stock opt -instcombine: %r = and i32 %x, 7
;   seaopt -sea-instcombine: %r = urem i32 %x, 8   (kept)
;
; RUN: seaopt -sea-instcombine -S < %s | FileCheck %s

define i32 @urem_pow2(i32 %x) {
  %r = urem i32 %x, 8
  ret i32 %r
}
; CHECK-LABEL: @urem_pow2
; CHECK: urem i32 %x, 8
; CHECK-NOT: and

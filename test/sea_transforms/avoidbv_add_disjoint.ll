; AvoidBv: "A + B -> A | B when A and B have no bits in common" is suppressed.
; Gated at InstCombineAddSub.cpp (`if (!AvoidBv && haveNoCommonBitsSet(...))`).
;
; %a occupies bits [0:1], %b occupies bits [2:3] -> disjoint.
; Validated on LLVM 14/18 (LLVM 18 tags the fold with the new `disjoint` flag):
;   stock opt -instcombine: %r = or disjoint i32 %a, %b
;   seaopt -passes=sea-instcombine: %r = add nuw nsw i32 %a, %b   (kept)
;
; RUN: %seaopt -passes=sea-instcombine -S %s | %FileCheck %s
; RUN: %seaopt -passes=sea-instcombine -S %s | %opt -passes=verify -disable-output
; RUN: %opt -passes=instcombine -S %s | %FileCheck --check-prefix=STOCK %s

define i32 @add_disjoint(i32 %x, i32 %y) {
  %a = and i32 %x, 3
  %b = and i32 %y, 12
  %r = add i32 %a, %b
  ret i32 %r
}
; CHECK-LABEL: @add_disjoint
; CHECK: %r = add{{.*}}i32 %a, %b
; CHECK-NOT: = or
;
; non-vacuity: stock instcombine turns the disjoint add into an or
; STOCK: = or {{(disjoint )?}}i32

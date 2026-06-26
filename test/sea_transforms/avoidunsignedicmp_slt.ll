; AvoidUnsignedICmp: turning a signed compare into an unsigned one when both
; operands are known to have the same sign is suppressed.
; Gated at InstCombineCompares.cpp (`if (!AvoidUnsignedICmp && I.isSigned() && ...)`).
;
; Masking with 255 makes the sign bit known-zero on both operands.
; Validated on LLVM 14:
;   stock opt -instcombine: %c = icmp ult i32 %a, %b
;   seaopt -passes=sea-instcombine: %c = icmp slt i32 %a, %b   (kept)
;
; RUN: %seaopt -passes=sea-instcombine -S %s | %FileCheck %s
; RUN: %seaopt -passes=sea-instcombine -S %s | %opt -passes=verify -disable-output
; RUN: %opt -passes=instcombine -S %s | %FileCheck --check-prefix=STOCK %s

define i1 @sicmp_to_uicmp(i32 %x, i32 %y) {
  %a = and i32 %x, 255
  %b = and i32 %y, 255
  %c = icmp slt i32 %a, %b
  ret i1 %c
}
; CHECK-LABEL: @sicmp_to_uicmp
; CHECK: icmp slt
; CHECK-NOT: icmp ult
;
; non-vacuity: stock instcombine flips the signed compare to unsigned
; STOCK: icmp ult i32

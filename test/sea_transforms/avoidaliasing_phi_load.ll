; AvoidAliasing: sinking/merging loads through a phi (FoldPHIArgLoadIntoPHI) is
; suppressed because the merged load can alias.
; Gated at InstCombinePHI.cpp (`isSafeAndProfitableToSinkLoad` returns false
; when AvoidAliasing is set).
;
; Validated on LLVM 14:
;   stock opt -instcombine: merges to  %v.in = phi i32* [...] ; %v = load i32, i32* %v.in
;   seaopt -sea-instcombine: keeps two separate loads + an i32 phi
;
; This also exercises pointer construction: the suppressed transform builds a
; pointer-typed phi + a new load. On LLVM 15 (opaque pointers) that merged form
; is `phi ptr`; a port regression here therefore also flags opaque-pointer drift.
; Typed-pointer syntax (i32*) is used so the test runs on both LLVM 14 and 15.
;
; RUN: %seaopt -sea-instcombine -S %s | %FileCheck %s
; RUN: %seaopt -sea-instcombine -S %s | %opt -passes=verify -disable-output
; RUN: %opt -passes=instcombine -S %s | %FileCheck --check-prefix=STOCK %s

define i32 @phi_load(i1 %c, i32* %p, i32* %q) {
entry:
  br i1 %c, label %t, label %f
t:
  %lt = load i32, i32* %p
  br label %m
f:
  %lf = load i32, i32* %q
  br label %m
m:
  %v = phi i32 [ %lt, %t ], [ %lf, %f ]
  ret i32 %v
}
; CHECK-LABEL: @phi_load
; both loads must survive (merging would leave only one), and the result phi
; stays an i32 phi rather than a pointer phi:
; CHECK-DAG: load i32, i32* %p
; CHECK-DAG: load i32, i32* %q
; CHECK: phi i32 [
;
; non-vacuity: stock instcombine sinks the loads, creating a pointer-typed phi
; STOCK: phi i32*

; AvoidAliasing: sinking/merging loads through a phi (FoldPHIArgLoadIntoPHI) is
; suppressed because the merged load can alias.
; Gated at InstCombinePHI.cpp (`isSafeAndProfitableToSinkLoad` returns false
; when AvoidAliasing is set).
;
; Behavior on LLVM 16 (opaque pointers):
;   stock opt -passes=instcombine: merges to  %v.in = phi ptr [...] ; %v = load i32, ptr %v.in
;   seaopt -sea-instcombine: keeps two separate loads + an i32 phi
;
; This also exercises pointer construction: the suppressed transform builds a
; pointer-typed phi + a new load (`phi ptr` under opaque pointers); a port
; regression here therefore also flags opaque-pointer drift.
;
; RUN: %seaopt -sea-instcombine -S %s | %FileCheck %s
; RUN: %seaopt -sea-instcombine -S %s | %opt -passes=verify -disable-output
; RUN: %opt -passes=instcombine -S %s | %FileCheck --check-prefix=STOCK %s

define i32 @phi_load(i1 %c, ptr %p, ptr %q) {
entry:
  br i1 %c, label %t, label %f
t:
  %lt = load i32, ptr %p
  br label %m
f:
  %lf = load i32, ptr %q
  br label %m
m:
  %v = phi i32 [ %lt, %t ], [ %lf, %f ]
  ret i32 %v
}
; CHECK-LABEL: @phi_load
; both loads must survive (merging would leave only one), and the result phi
; stays an i32 phi rather than a pointer phi:
; CHECK-DAG: load i32, ptr %p
; CHECK-DAG: load i32, ptr %q
; CHECK: phi i32 [
;
; non-vacuity: stock instcombine sinks the loads, creating a pointer-typed phi
; STOCK: phi ptr

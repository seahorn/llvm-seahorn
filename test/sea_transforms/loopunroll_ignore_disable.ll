; SeaHorn LoopUnroll ignores the `llvm.loop.unroll.disable` metadata.
; In tryToUnrollLoop (LoopUnrollPass.cc), where stock LLVM bails out when
; TM_Disable is set, SeaHorn only logs ("Forcing Loop Unroll despite disable
; metadata") and falls through, so a small constant-trip loop is still unrolled.
;
; Validated on LLVM 14 (constant trip count = 4):
;   stock opt -loop-unroll:      respects metadata -> loop kept, 1 store
;   seaopt -sea-loop-unroll:     ignores metadata  -> fully unrolled, 4 stores, no phi
;
; RUN: %seaopt -sea-loop-unroll -S %s | %FileCheck %s
; RUN: %seaopt -sea-loop-unroll -S %s | %opt -passes=verify -disable-output
; RUN: %opt -passes=loop-unroll -S %s | %FileCheck --check-prefix=STOCK %s

define void @u(i32* %a) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %inc, %loop ]
  %p = getelementptr inbounds i32, i32* %a, i32 %i
  store i32 %i, i32* %p
  %inc = add nuw nsw i32 %i, 1
  %c = icmp slt i32 %inc, 4
  br i1 %c, label %loop, label %exit, !llvm.loop !0
exit:
  ret void
}
!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}

; CHECK-LABEL: @u
; fully unrolled: the 4th iteration's store is present and the loop phi is gone
; CHECK: store i32 3,
; CHECK-NOT: = phi
;
; non-vacuity: stock respects the disable metadata and keeps the loop (a phi)
; STOCK: = phi

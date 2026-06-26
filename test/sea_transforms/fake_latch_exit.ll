; SeaFakeLatchExit: a loop whose latch ends in an *unconditional* branch gets a
; fake, statically-always-taken exit edge. The latch
;   latch: br label %header
; becomes
;   latch: br i1 true, label %header, label %fake_latch_exit
;   fake_latch_exit: unreachable
; giving the loop a structural exit block. This is a SeaHorn-only transform with
; no stock instcombine/-O counterpart, so (unlike the avoid* tests) there is no
; STOCK: non-vacuity line -- the pass is exercised directly via -passes.
;
; Run it as a new-PM function pass (it lives at the function level, where it can
; see LoopInfo). Must run LAST in a real pipeline: simplifycfg/instcombine fold
; `br i1 true` back to an unconditional branch.
;
; RUN: %seaopt -passes='function(sea-fake-latch-exit)' -S %s | %FileCheck %s
; RUN: %seaopt -passes='function(sea-fake-latch-exit)' -S %s | %opt -passes=verify -disable-output

; The exiting test is in %header; the latch (%latch) is a separate block with an
; unconditional backedge -- exactly the shape SeaFakeLatchExit rewrites.
define void @f(i32 %n) {
entry:
  br label %header
header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %latch ]
  %c = icmp slt i32 %i, %n
  br i1 %c, label %body, label %exit
body:
  %i.next = add i32 %i, 1
  br label %latch
latch:
  br label %header
exit:
  ret void
}

; CHECK-LABEL: @f
; the unconditional latch branch becomes a statically-true conditional one whose
; false edge targets a fresh unreachable block:
; CHECK: br i1 true, label %header, label %fake_latch_exit
; CHECK: fake_latch_exit:
; CHECK-NEXT: unreachable

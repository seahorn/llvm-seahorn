; Pipeline-level test: run the full SeaHorn -O2 pipeline (PassManagerBuilder),
; which wires in sea-instcombine, the sea loop-rotate, sea-indvars and
; sea-loop-unroll -- not a single pass in isolation.
;
; Two things are checked:
;   - Behavioral: the SeaHorn instcombine is actually engaged in the pipeline.
;     `urem` by a power of two survives `seaopt -O2`, whereas stock `opt -O2`
;     folds it to `and`. This catches a wiring regression where the pipeline
;     picks up stock InstCombine instead of createSeaInstructionCombiningPass().
;   - Smoke/verify: @sum drives the loop passes (rotate / indvars / unroll) as
;     part of -O2; the verifier RUN line asserts the whole pipeline output is
;     well-formed. This is the only coverage of the sea loop-rotate and
;     sea-indvars passes -- their *behavioral* divergence from stock is not
;     observable on LLVM 14 (stock LFTR never emits the disequality, and large
;     headers either fold away or block rotation), so this guards against
;     crashes / malformed IR (e.g. opaque-pointer breakage during the port)
;     rather than asserting a specific rewrite.
;
; RUN: %seaopt -O2 -S %s | %FileCheck %s
; RUN: %seaopt -O2 -S %s | %opt -passes=verify -disable-output
; RUN: %opt -O2 -S %s | %FileCheck --check-prefix=STOCK %s

define i32 @urem_pow2(i32 %x) {
  %r = urem i32 %x, 8
  ret i32 %r
}

define i32 @sum(i32* %a, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %p = getelementptr inbounds i32, i32* %a, i32 %i
  %v = load i32, i32* %p
  %s.next = add i32 %s, %v
  %i.next = add nsw i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %s
}

; sea-instcombine is in the -O2 pipeline -> urem survives, no power-of-2 and
; CHECK: urem i32 %x, 8
; CHECK-NOT: and i32 %x, 7
;
; non-vacuity: stock -O2 folds urem-by-pow2 to and
; STOCK: and i32 %x, 7

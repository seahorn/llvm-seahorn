; Loop-extract / replaceFnBodyWithND behavioral test, driven by the standalone
; sea_loop_extract_driver (which runs createSeaLoopExtractorPass over the module
; and verifies the result). The driver exits non-zero on invalid IR, so the
; void-return / opaque-pointer bug in replaceFnBodyWithND makes this RUN fail.
;
; The single loop here is extracted (its exit goes to a block that does work,
; not a plain return), and the extracted function's body is replaced with
; non-deterministic verifier.nondet.* stubs.
;
; RUN: %sea-loop-extract-driver %s | %FileCheck %s

define i32 @f(i32* %a, i32 %n) {
entry:
  %g = icmp sgt i32 %n, 0
  br i1 %g, label %loop, label %done
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s.next, %loop ]
  %p = getelementptr inbounds i32, i32* %a, i32 %i
  %v = load i32, i32* %p
  %s.next = add i32 %s, %v
  %i.next = add nsw i32 %i, 1
  %c = icmp slt i32 %i.next, %n
  br i1 %c, label %loop, label %after
after:
  %x = mul i32 %s.next, 2
  br label %done
done:
  %r = phi i32 [ 0, %entry ], [ %x, %after ]
  ret i32 %r
}

; the loop is extracted into an internal, void-returning function ...
; CHECK: define internal void @f.loop(
; ... whose body is a non-deterministic stub
; CHECK: call i32 @verifier.nondet

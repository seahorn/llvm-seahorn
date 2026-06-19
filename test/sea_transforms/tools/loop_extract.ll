; Input for sea_loop_extract_driver: a single loop that the SeaLoopExtractor
; will extract (its exit goes to a block that does work, not a plain return),
; so replaceFnBodyWithND replaces the extracted function body with
; verifier.nondet.* stubs.
;
; Expected: the loop is extracted into an internal void function whose body is
; verifier.nondet.* calls + stores into the output args, and the module verifies.

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

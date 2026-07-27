; M2 smoke test: direct VMP instruction selection, PHI edge copies, stack-slot
; layout, REL32 fixup and pure VMPC stream emission.
target datalayout = "e-m:e-p:64:64-i64:64-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

define i64 @target_loop(i64 %count, ptr %out) {
entry:
  %slot = alloca i64, align 8
  store i64 0, ptr %slot, align 8
  br label %loop

loop:
  %index = phi i64 [ 0, %entry ], [ %next, %body ]
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %body ]
  %done = icmp uge i64 %index, %count
  br i1 %done, label %exit, label %body

body:
  %sum.next = add i64 %sum, %index
  store i64 %sum.next, ptr %slot, align 8
  %next = add i64 %index, 1
  br label %loop

exit:
  %result = load i64, ptr %slot, align 8
  store i64 %result, ptr %out, align 8
  ret i64 %result
}

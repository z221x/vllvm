@fallback_text = private constant [9 x i8] c"fallback\00"

; No partial lowering: if the callbr-result PHI is unsupported, preserve the
; string PHI too and do not encrypt its incoming literal.
define ptr @unsupported_phi() #0 {
entry:
  %value = callbr i32 asm sideeffect "", "=r,!i"() to label %join [label %other]
other:
  br label %join
join:
  %text = phi ptr [ @fallback_text, %entry ], [ null, %other ]
  %number = phi i32 [ %value, %entry ], [ 1, %other ]
  %ok = icmp ne i32 %number, 0
  %result = select i1 %ok, ptr %text, ptr null
  ret ptr %result
}

attributes #0 = { noinline }

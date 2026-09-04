@left_text = private constant [5 x i8] c"left\00"
@right_text = private constant [6 x i8] c"right\00"

; Deliberately no PHI debug location: generated stores must still get a valid
; scope before enstr inserts an inlinable decryptor call.
define ptr @select_string(i1 %flag) #0 !dbg !4 {
entry:
  br i1 %flag, label %left, label %right
left:
  br label %join
right:
  br label %join
join:
  %text = phi ptr [ @left_text, %left ], [ @right_text, %right ]
  %unused = phi i32 [ 1, %left ], [ 2, %right ]
  ret ptr %text, !dbg !7
}

define i32 @critical_edge(i1 %flag, i32 %a, i32 %b) #0 {
entry:
  br i1 %flag, label %join, label %other
other:
  %next = add i32 %b, 3
  br label %join
join:
  %value = phi i32 [ %a, %entry ], [ %next, %other ]
  ret i32 %value
}

define i32 @duplicate_edges(i32 %index) #0 {
entry:
  switch i32 %index, label %other [i32 0, label %join
                                 i32 1, label %join]
other:
  br label %join
join:
  %value = phi i32 [ 7, %entry ], [ 7, %entry ], [ 9, %other ]
  ret i32 %value
}

; Mutually dependent loop PHIs must retain parallel-copy semantics.
define i32 @loop_swap(i32 %limit) #0 {
entry:
  br label %loop
loop:
  %a = phi i32 [ 11, %entry ], [ %b, %loop ]
  %b = phi i32 [ 29, %entry ], [ %a, %loop ]
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %next = add i32 %i, 1
  %again = icmp slt i32 %next, %limit
  br i1 %again, label %loop, label %exit
exit:
  %high = mul i32 %a, 100
  %result = add i32 %high, %b
  ret i32 %result
}

declare i32 @phi_may_throw(i32)
declare i32 @__gxx_personality_v0(...)

define i32 @invoke_phi(i1 %flag, i32 %x) #0 personality ptr @__gxx_personality_v0 {
entry:
  br i1 %flag, label %call, label %other
call:
  %value = invoke i32 @phi_may_throw(i32 %x) to label %join unwind label %unwind
other:
  br label %join
join:
  %result = phi i32 [ %value, %call ], [ 77, %other ]
  ret i32 %result
unwind:
  %exception = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %exception
}

define ptr @invoke_string(i1 %flag, i32 %x) #0 personality ptr @__gxx_personality_v0 {
entry:
  br i1 %flag, label %call, label %other
call:
  %ignored = invoke i32 @phi_may_throw(i32 %x) to label %join unwind label %unwind
other:
  br label %join
join:
  %text = phi ptr [ @left_text, %call ], [ @right_text, %other ]
  ret ptr %text
unwind:
  %exception = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %exception
}

attributes #0 = { noinline }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}
!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "VLLVM PHI test", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "test_phi_lowering.ll", directory: ".")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = distinct !DISubprogram(name: "select_string", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{null}
!7 = !DILocation(line: 1, column: 1, scope: !4)

@left_text = private constant [5 x i8] c"left\00"
@right_text = private constant [6 x i8] c"right\00"
declare void @may_throw()
declare i32 @__CxxFrameHandler3(...)

; PHIs inside an ordinary funclet block are supported. The decryptor calls
; inserted before their lowered stores must carry the catchpad operand bundle.
define ptr @funclet_string(i1 %flag) #0 personality ptr @__CxxFrameHandler3 {
entry:
  invoke void @may_throw() to label %exit unwind label %dispatch
dispatch:
  %cs = catchswitch within none [label %catch] unwind to caller
catch:
  %cp = catchpad within %cs [ptr null, i32 64, ptr null]
  br i1 %flag, label %left, label %right
left:
  br label %join
right:
  br label %join
join:
  %text = phi ptr [ @left_text, %left ], [ @right_text, %right ]
  catchret from %cp to label %handled
handled:
  ret ptr %text
exit:
  ret ptr null
}

attributes #0 = { noinline "vllvm.enstr" }

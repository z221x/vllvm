@fallback_text = private constant [9 x i8] c"fallback\00"
declare void @may_throw()
declare i32 @__CxxFrameHandler3(...)

define ptr @unsupported_phi(i1 %flag) #0 personality ptr @__CxxFrameHandler3 {
entry:
  br i1 %flag, label %left, label %right
left:
  invoke void @may_throw() to label %exit unwind label %dispatch
right:
  invoke void @may_throw() to label %exit unwind label %dispatch
dispatch:
  %text = phi ptr [ @fallback_text, %left ], [ null, %right ]
  %cs = catchswitch within none [label %catch] unwind to caller
catch:
  %cp = catchpad within %cs [ptr null, i32 64, ptr null]
  catchret from %cp to label %handled
handled:
  ret ptr %text
exit:
  ret ptr null
}

attributes #0 = { noinline }

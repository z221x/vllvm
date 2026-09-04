@left_text = private constant [5 x i8] c"left\00"
@right_text = private constant [6 x i8] c"right\00"
@left_ptr = internal global ptr @left_text
@right_ptr = internal global ptr @right_text

define ptr @pick_string(i1 %flag) "vllvm.enstr" {
entry:
  br i1 %flag, label %left, label %right
left:
  br label %join
right:
  br label %join
join:
  %text = phi ptr [ @left_text, %left ], [ @right_text, %right ]
  %unused = phi i32 [ 1, %left ], [ 2, %right ]
  ret ptr %text
}

define ptr @pick_pointer(i1 %flag) "vllvm.enstr" {
entry:
  br i1 %flag, label %left, label %right
left:
  br label %join
right:
  br label %join
join:
  %slot = phi ptr [ @left_ptr, %left ], [ @right_ptr, %right ]
  %text = load ptr, ptr %slot
  ret ptr %text
}

; Two edges from one predecessor must retain the same PHI incoming value.
define ptr @duplicate_edges(i32 %index) "vllvm.enstr" {
entry:
  switch i32 %index, label %right [i32 0, label %join
                                 i32 1, label %join]
right:
  br label %join
join:
  %text = phi ptr [ @left_text, %entry ], [ @left_text, %entry ],
                  [ @right_text, %right ]
  ret ptr %text
}

define ptr @loop_phi(i32 %limit) "vllvm.enstr" {
entry:
  br label %loop
loop:
  %text = phi ptr [ @left_text, %entry ], [ %text, %loop ]
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %next = add i32 %i, 1
  %again = icmp slt i32 %next, %limit
  br i1 %again, label %loop, label %exit
exit:
  ret ptr %text
}

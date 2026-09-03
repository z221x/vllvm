define i32 @ibr_phi(i1 %condition, i32 %value) #0 {
entry:
  br i1 %condition, label %left, label %right

left:
  %left.value = add i32 %value, 11
  br label %join

right:
  %right.value = sub i32 %value, 7
  br label %join

join:
  %merged = phi i32 [ %left.value, %left ], [ %right.value, %right ]
  %positive.condition = icmp sgt i32 %merged, 0
  br i1 %positive.condition, label %positive, label %negative

positive:
  %positive.value = mul i32 %merged, 3
  br label %exit

negative:
  %negative.value = xor i32 %merged, 85
  br label %exit

exit:
  %result = phi i32 [ %positive.value, %positive ],
                    [ %negative.value, %negative ]
  ret i32 %result
}

attributes #0 = { noinline "vllvm.ibr" }

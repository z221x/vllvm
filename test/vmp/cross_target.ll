define i64 @cross_target_add(i64 %left, i64 %right) #0 {
entry:
  %sum = add i64 %left, %right
  %mixed = xor i64 %sum, 1311768467463790320
  ret i64 %mixed
}

define internal fastcc i64 @cross_host_eight(i64 %a0, i64 %a1, i64 %a2,
                                             i64 %a3, i64 %a4, i64 %a5,
                                             i64 %a6, i64 %a7) #1 {
entry:
  %v0 = add i64 %a0, %a1
  %v1 = add i64 %v0, %a2
  %v2 = add i64 %v1, %a3
  %v3 = add i64 %v2, %a4
  %v4 = add i64 %v3, %a5
  %v5 = add i64 %v4, %a6
  %v6 = add i64 %v5, %a7
  ret i64 %v6
}

define i64 @cross_target_call(i64 %left, i64 %right) #0 {
entry:
  %result = call fastcc i64 @cross_host_eight(
      i64 %left, i64 %right, i64 3, i64 4,
      i64 5, i64 6, i64 7, i64 8)
  ret i64 %result
}

attributes #0 = { "vllvm.vmp" }
attributes #1 = { noinline }

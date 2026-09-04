; A deliberately different SDK version must survive embedding the VMP runtime.
define i32 @protected_sdk(i32 %value) #0 {
entry:
  %result = add i32 %value, 7
  ret i32 %result
}

attributes #0 = { noinline "vllvm.vmp" }

!llvm.module.flags = !{!0}
!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 99, i32 1]}

target datalayout = "e-m:e-p:64:64-i64:64-n32:64-S128"
target triple = "bpfel-unknown-none"

declare i64 @__vllvm_vmp_hostcall.0(i64, i64, i64, i64, i64, i64)

define i64 @target_hostcall(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e,
                            i64 %f) section ".vmp.code" {
entry:
  %result = call i64 @__vllvm_vmp_hostcall.0(
      i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f)
  ret i64 %result
}

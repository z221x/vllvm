target triple = "aarch64-unknown-linux-gnu"

declare fastcc i64 @external_fast_target(i64)

define i64 @unsafe_fastcc_call(i64 %value) #0 {
entry:
  %result = call fastcc i64 @external_fast_target(i64 %value)
  ret i64 %result
}

attributes #0 = { "vllvm.vmp" }

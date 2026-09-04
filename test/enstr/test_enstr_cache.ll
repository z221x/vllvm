@alpha_text = private constant [12 x i8] c"cache-alpha\00"
@beta_text = private constant [11 x i8] c"cache-beta\00"
@alpha_pointer = internal global ptr @alpha_text

define ptr @cached_alpha() "vllvm.enstr" {
  ret ptr @alpha_text
}

; A global pointer and direct uses of the same string must share one cache.
define ptr @cached_alpha_alias() "vllvm.enstr" {
  %text = load ptr, ptr @alpha_pointer
  ret ptr %text
}

define ptr @cached_beta() "vllvm.enstr" {
  ret ptr @beta_text
}

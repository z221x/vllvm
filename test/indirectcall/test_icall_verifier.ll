target triple = "aarch64-unknown-linux-gnu"

declare void @ordinary(ptr nest)

define icallcc i32 @dispatch(i32 %value, i32 nest %index) {
  ret i32 %value
}

define i32 @caller() {
  %value = call icallcc i32 @dispatch(i32 42, i32 nest 65537)
  ret i32 %value
}

; ModuleID = './test_indirectcall.c'
source_filename = "./test_indirectcall.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [15 x i8] c"This is funcA\0A\00", align 1
@.str.1 = private unnamed_addr constant [15 x i8] c"This is funcB\0A\00", align 1
@.str.2 = private unnamed_addr constant [15 x i8] c"This is funcC\0A\00", align 1
@.str.3 = private unnamed_addr constant [15 x i8] c"This is funcD\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @funcA() #0 {
  %1 = call i32 (ptr, ...) @printf(ptr noundef @.str)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @funcB() #0 {
  %1 = call i32 (ptr, ...) @printf(ptr noundef @.str.1)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @funcC() #0 {
  %1 = call i32 (ptr, ...) @printf(ptr noundef @.str.2)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @funcD() #0 {
  %1 = call i32 (ptr, ...) @printf(ptr noundef @.str.3)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  call void @funcA()
  call void @funcB()
  call void @funcC()
  call void @funcD()
  ret i32 0
}

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 21.1.0 (https://github.com/llvm/llvm-project.git 3623fe661ae35c6c80ac221f14d85be76aa870f1)"}

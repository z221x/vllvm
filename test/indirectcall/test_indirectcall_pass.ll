; ModuleID = './test_indirectcall.ll'
source_filename = "./test_indirectcall.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [15 x i8] c"This is funcA\0A\00", align 1
@.str.1 = private unnamed_addr constant [15 x i8] c"This is funcB\0A\00", align 1
@.str.2 = private unnamed_addr constant [15 x i8] c"This is funcC\0A\00", align 1
@.str.3 = private unnamed_addr constant [15 x i8] c"This is funcD\0A\00", align 1
@func_tablefuncA = private constant [0 x ptr] zeroinitializer
@func_tablefuncB = private constant [0 x ptr] zeroinitializer
@func_tablefuncC = private constant [0 x ptr] zeroinitializer
@func_tablefuncD = private constant [0 x ptr] zeroinitializer
@func_tablemain = private constant [4 x ptr] [ptr getelementptr (ptr, ptr @funcC, i32 940744832), ptr getelementptr (ptr, ptr @funcB, i32 481651939), ptr getelementptr (ptr, ptr @funcA, i32 667850708), ptr getelementptr (ptr, ptr @funcD, i32 2017640622)]

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
  %1 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 2), align 8
  %2 = getelementptr i64, ptr %1, i32 -667850708
  call void %2()
  %3 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 1), align 8
  %4 = getelementptr i64, ptr %3, i32 -481651939
  call void %4()
  %5 = load ptr, ptr @func_tablemain, align 8
  %6 = getelementptr i64, ptr %5, i32 -940744832
  call void %6()
  %7 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 3), align 8
  %8 = getelementptr i64, ptr %7, i32 -2017640622
  call void %8()
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

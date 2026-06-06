; ModuleID = './test_indirectbr.c'
source_filename = "./test_indirectbr.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [15 x i8] c"This is funcF\0A\00", align 1
@.str.1 = private unnamed_addr constant [5 x i8] c"BB1\0A\00", align 1
@.str.2 = private unnamed_addr constant [5 x i8] c"BB2\0A\00", align 1
@.str.3 = private unnamed_addr constant [5 x i8] c"BB3\0A\00", align 1
@.str.4 = private unnamed_addr constant [5 x i8] c"BB4\0A\00", align 1
@.str.5 = private unnamed_addr constant [15 x i8] c"This is funcM\0A\00", align 1
@.str.6 = private unnamed_addr constant [15 x i8] c"This is func1\0A\00", align 1
@.str.7 = private unnamed_addr constant [15 x i8] c"This is func2\0A\00", align 1
@.str.8 = private unnamed_addr constant [15 x i8] c"This is func3\0A\00", align 1
@.str.9 = private unnamed_addr constant [15 x i8] c"This is func4\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @funcE() #0 {
  ret i32 1
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @funcF() #0 {
  %1 = call i32 (ptr, ...) @printf(ptr noundef @.str)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  %2 = call i32 (ptr, ...) @printf(ptr noundef @.str.1)
  br label %3

3:                                                ; preds = %0
  %4 = call i32 (ptr, ...) @printf(ptr noundef @.str.2)
  br label %5

5:                                                ; preds = %3
  %6 = call i32 (ptr, ...) @printf(ptr noundef @.str.3)
  br label %7

7:                                                ; preds = %5
  %8 = call i32 (ptr, ...) @printf(ptr noundef @.str.4)
  br label %9

9:                                                ; preds = %7
  %10 = call i32 @funcE()
  %11 = icmp ne i32 %10, 0
  br i1 %11, label %12, label %13

12:                                               ; preds = %9
  call void @funcF()
  br label %15

13:                                               ; preds = %9
  %14 = call i32 (ptr, ...) @printf(ptr noundef @.str.5)
  br label %15

15:                                               ; preds = %13, %12
  %16 = call i32 @funcE()
  switch i32 %16, label %25 [
    i32 1, label %17
    i32 2, label %19
    i32 3, label %21
    i32 4, label %23
  ]

17:                                               ; preds = %15
  %18 = call i32 (ptr, ...) @printf(ptr noundef @.str.6)
  br label %25

19:                                               ; preds = %15
  %20 = call i32 (ptr, ...) @printf(ptr noundef @.str.7)
  br label %25

21:                                               ; preds = %15
  %22 = call i32 (ptr, ...) @printf(ptr noundef @.str.8)
  br label %25

23:                                               ; preds = %15
  %24 = call i32 (ptr, ...) @printf(ptr noundef @.str.9)
  br label %25

25:                                               ; preds = %15, %23, %21, %19, %17
  %26 = load i32, ptr %1, align 4
  ret i32 %26
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

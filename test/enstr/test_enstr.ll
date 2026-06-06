; ModuleID = './test_enstr.c'
source_filename = "./test_enstr.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [14 x i8] c"This is func2\00", align 1
@a1 = dso_local global ptr @.str, align 8
@.str.1 = private unnamed_addr constant [14 x i8] c"This is func3\00", align 1
@a2 = dso_local global ptr @.str.1, align 8
@.str.2 = private unnamed_addr constant [14 x i8] c"This is func1\00", align 1
@.str.3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@.str.4 = private unnamed_addr constant [4 x i8] c"%x\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local ptr @func1() #0 {
  ret ptr @.str.2
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @func2() #0 {
  %1 = load ptr, ptr @a1, align 8
  %2 = call i32 (ptr, ...) @printf(ptr noundef @.str.3, ptr noundef %1)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @func3() #0 {
  %1 = alloca ptr, align 8
  store ptr @a2, ptr %1, align 8
  %2 = load ptr, ptr %1, align 8
  %3 = load ptr, ptr %2, align 8
  %4 = call i32 (ptr, ...) @printf(ptr noundef @.str.3, ptr noundef %3)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = call ptr @func1()
  %2 = call i32 (ptr, ...) @printf(ptr noundef @.str.3, ptr noundef %1)
  call void @func2()
  call void @func3()
  %3 = call i32 (ptr, ...) @printf(ptr noundef @.str.4, i32 noundef 1956577150)
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

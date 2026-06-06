; ModuleID = './test_enstr.ll'
source_filename = "./test_enstr.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr global [14 x i8] c"}\9E\DB\1E\B9\E6\CC\8E\B5n\97\DC'T", align 1
@a1 = dso_local global ptr @.str, align 8
@.str.1 = private unnamed_addr global [14 x i8] c"\1D=\AE\91\10o\FC\91\EE_\A6\1E\F4\BF", align 1
@a2 = dso_local global ptr @.str.1, align 8
@.str.2 = private unnamed_addr global [14 x i8] c"G\105\9B\C6\F9W\85{\04R\DA\883", align 1
@.str.3 = private unnamed_addr global [4 x i8] c"\D25\1Ai", align 1
@.str.4 = private unnamed_addr global [4 x i8] c"\84\E5\82\A1", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local ptr @func1() #0 {
  %1 = call ptr @_decrypto5(ptr @.str.2)
  ret ptr %1
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @func2() #0 {
  %1 = alloca ptr, align 8
  %2 = load ptr, ptr @a1, align 8
  %3 = call ptr @_decrypto1(ptr %2)
  store ptr %3, ptr %1, align 8
  %4 = load ptr, ptr %1, align 8
  %5 = call ptr @_decrypto6(ptr @.str.3)
  %6 = call i32 (ptr, ...) @printf(ptr noundef %5, ptr noundef %4)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @func3() #0 {
  %1 = alloca ptr, align 8
  %2 = alloca ptr, align 8
  %3 = load ptr, ptr @a2, align 8
  %4 = call ptr @_decrypto3(ptr %3)
  store ptr %4, ptr %2, align 8
  store ptr %2, ptr %1, align 8
  %5 = load ptr, ptr %1, align 8
  %6 = load ptr, ptr %5, align 8
  %7 = call ptr @_decrypto6(ptr @.str.3)
  %8 = call i32 (ptr, ...) @printf(ptr noundef %7, ptr noundef %6)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = call ptr @func1()
  %2 = call ptr @_decrypto6(ptr @.str.3)
  %3 = call i32 (ptr, ...) @printf(ptr noundef %2, ptr noundef %1)
  call void @func2()
  call void @func3()
  %4 = call ptr @_decrypto7(ptr @.str.4)
  %5 = call i32 (ptr, ...) @printf(ptr noundef %4, i32 noundef 1956577150)
  ret i32 0
}

declare ptr @malloc(i64)

define private ptr @_decrypto1(ptr %strArg) {
entry:
  %0 = call ptr @malloc(i64 14)
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 41, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -10, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 -78, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 109, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 -103, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -113, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -65, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -82, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -45, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 27, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -7, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -65, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 21, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 84, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 118, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 97, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  br label %loop

loop:                                             ; preds = %loopbr, %entry
  %iLoad = load i64, ptr %i, align 8
  %cond = icmp slt i64 %iLoad, 14
  br i1 %cond, label %loopbr, label %exit

loopbr:                                           ; preds = %loop
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad
  %strLoad = load i8, ptr %strPtr, align 1
  %keyPtr = getelementptr i8, ptr %key, i64 %iLoad
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %outPtr = getelementptr i8, ptr %0, i64 %iLoad
  store i8 %xorValue, ptr %outPtr, align 1
  %iNext = add i64 %iLoad, 1
  store i64 %iNext, ptr %i, align 8
  br label %loop

exit:                                             ; preds = %loop
  ret ptr %0
}

define private ptr @_decrypto3(ptr %strArg) {
entry:
  %0 = call ptr @malloc(i64 14)
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 73, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 85, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 -57, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -30, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 48, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 6, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -113, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -79, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -120, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 42, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -56, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 125, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -57, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -65, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -117, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 62, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  br label %loop

loop:                                             ; preds = %loopbr, %entry
  %iLoad = load i64, ptr %i, align 8
  %cond = icmp slt i64 %iLoad, 14
  br i1 %cond, label %loopbr, label %exit

loopbr:                                           ; preds = %loop
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad
  %strLoad = load i8, ptr %strPtr, align 1
  %keyPtr = getelementptr i8, ptr %key, i64 %iLoad
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %outPtr = getelementptr i8, ptr %0, i64 %iLoad
  store i8 %xorValue, ptr %outPtr, align 1
  %iNext = add i64 %iLoad, 1
  store i64 %iNext, ptr %i, align 8
  br label %loop

exit:                                             ; preds = %loop
  ret ptr %0
}

define private ptr @_decrypto5(ptr %strArg) {
entry:
  %0 = call ptr @malloc(i64 14)
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 19, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 120, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 92, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -24, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 -26, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -112, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 36, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -91, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 29, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 113, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 60, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -71, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -71, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 51, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 60, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 -97, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  br label %loop

loop:                                             ; preds = %loopbr, %entry
  %iLoad = load i64, ptr %i, align 8
  %cond = icmp slt i64 %iLoad, 14
  br i1 %cond, label %loopbr, label %exit

loopbr:                                           ; preds = %loop
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad
  %strLoad = load i8, ptr %strPtr, align 1
  %keyPtr = getelementptr i8, ptr %key, i64 %iLoad
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %outPtr = getelementptr i8, ptr %0, i64 %iLoad
  store i8 %xorValue, ptr %outPtr, align 1
  %iNext = add i64 %iLoad, 1
  store i64 %iNext, ptr %i, align 8
  br label %loop

exit:                                             ; preds = %loop
  ret ptr %0
}

define private ptr @_decrypto6(ptr %strArg) {
entry:
  %0 = call ptr @malloc(i64 4)
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -9, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 70, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 16, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 105, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 96, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -52, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 8, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 24, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -38, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 -98, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -33, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -101, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -6, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -82, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -19, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 66, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  br label %loop

loop:                                             ; preds = %loopbr, %entry
  %iLoad = load i64, ptr %i, align 8
  %cond = icmp slt i64 %iLoad, 4
  br i1 %cond, label %loopbr, label %exit

loopbr:                                           ; preds = %loop
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad
  %strLoad = load i8, ptr %strPtr, align 1
  %keyPtr = getelementptr i8, ptr %key, i64 %iLoad
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %outPtr = getelementptr i8, ptr %0, i64 %iLoad
  store i8 %xorValue, ptr %outPtr, align 1
  %iNext = add i64 %iLoad, 1
  store i64 %iNext, ptr %i, align 8
  br label %loop

exit:                                             ; preds = %loop
  ret ptr %0
}

define private ptr @_decrypto7(ptr %strArg) {
entry:
  %0 = call ptr @malloc(i64 4)
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -95, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -99, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 -120, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -95, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 108, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -8, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -72, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -52, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -90, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 3, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 94, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 68, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 35, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 1, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 94, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 -41, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  br label %loop

loop:                                             ; preds = %loopbr, %entry
  %iLoad = load i64, ptr %i, align 8
  %cond = icmp slt i64 %iLoad, 4
  br i1 %cond, label %loopbr, label %exit

loopbr:                                           ; preds = %loop
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad
  %strLoad = load i8, ptr %strPtr, align 1
  %keyPtr = getelementptr i8, ptr %key, i64 %iLoad
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %outPtr = getelementptr i8, ptr %0, i64 %iLoad
  store i8 %xorValue, ptr %outPtr, align 1
  %iNext = add i64 %iLoad, 1
  store i64 %iNext, ptr %i, align 8
  br label %loop

exit:                                             ; preds = %loop
  ret ptr %0
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

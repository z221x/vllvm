; ModuleID = './test_flatten.cpp'
source_filename = "./test_flatten.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [7 x i8] c"first\0A\00", align 1
@.str.1 = private unnamed_addr constant [8 x i8] c"second\0A\00", align 1
@.str.2 = private unnamed_addr constant [7 x i8] c"third\0A\00", align 1
@.str.3 = private unnamed_addr constant [8 x i8] c"fourth\0A\00", align 1
@_ZTIPKc = external constant ptr
@.str.4 = private unnamed_addr constant [7 x i8] c"fifth\0A\00", align 1
@.str.5 = private unnamed_addr constant [6 x i8] c"sixth\00", align 1
@.str.6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

; Function Attrs: mustprogress noinline norecurse optnone uwtable
define dso_local noundef i32 @main() #0 personality ptr @__gxx_personality_v0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  %5 = alloca ptr, align 8
  store i32 0, ptr %1, align 4
  store i32 0, ptr %2, align 4
  %6 = call i32 (ptr, ...) @printf(ptr noundef @.str)
  store i32 2, ptr %2, align 4
  %7 = load i32, ptr %2, align 4
  %8 = icmp ne i32 %7, 0
  br i1 %8, label %9, label %11

9:                                                ; preds = %0
  %10 = call i32 (ptr, ...) @printf(ptr noundef @.str.1)
  br label %11

11:                                               ; preds = %9, %0
  br label %12

12:                                               ; preds = %11, %19
  %13 = load i32, ptr %2, align 4
  switch i32 %13, label %18 [
    i32 2, label %14
    i32 3, label %16
  ]

14:                                               ; preds = %12
  %15 = call i32 (ptr, ...) @printf(ptr noundef @.str.2)
  store i32 3, ptr %2, align 4
  br label %19

16:                                               ; preds = %12
  %17 = call i32 (ptr, ...) @printf(ptr noundef @.str.3)
  br label %20

18:                                               ; preds = %12
  br label %19

19:                                               ; preds = %18, %14
  br label %12, !llvm.loop !6

20:                                               ; preds = %16
  %21 = invoke i32 (ptr, ...) @printf(ptr noundef @.str.4)
          to label %22 unwind label %24

22:                                               ; preds = %20
  %23 = call ptr @__cxa_allocate_exception(i64 8) #3
  store ptr @.str.5, ptr %23, align 16
  invoke void @__cxa_throw(ptr %23, ptr @_ZTIPKc, ptr null) #4
          to label %48 unwind label %24

24:                                               ; preds = %22, %20
  %25 = landingpad { ptr, i32 }
          catch ptr @_ZTIPKc
  %26 = extractvalue { ptr, i32 } %25, 0
  store ptr %26, ptr %3, align 8
  %27 = extractvalue { ptr, i32 } %25, 1
  store i32 %27, ptr %4, align 4
  br label %28

28:                                               ; preds = %24
  %29 = load i32, ptr %4, align 4
  %30 = call i32 @llvm.eh.typeid.for.p0(ptr @_ZTIPKc) #3
  %31 = icmp eq i32 %29, %30
  br i1 %31, label %32, label %43

32:                                               ; preds = %28
  %33 = load ptr, ptr %3, align 8
  %34 = call ptr @__cxa_begin_catch(ptr %33) #3
  store ptr %34, ptr %5, align 8
  %35 = load ptr, ptr %5, align 8
  %36 = invoke i32 (ptr, ...) @printf(ptr noundef @.str.6, ptr noundef %35)
          to label %37 unwind label %39

37:                                               ; preds = %32
  call void @__cxa_end_catch() #3
  br label %38

38:                                               ; preds = %37
  ret i32 0

39:                                               ; preds = %32
  %40 = landingpad { ptr, i32 }
          cleanup
  %41 = extractvalue { ptr, i32 } %40, 0
  store ptr %41, ptr %3, align 8
  %42 = extractvalue { ptr, i32 } %40, 1
  store i32 %42, ptr %4, align 4
  call void @__cxa_end_catch() #3
  br label %43

43:                                               ; preds = %39, %28
  %44 = load ptr, ptr %3, align 8
  %45 = load i32, ptr %4, align 4
  %46 = insertvalue { ptr, i32 } poison, ptr %44, 0
  %47 = insertvalue { ptr, i32 } %46, i32 %45, 1
  resume { ptr, i32 } %47

48:                                               ; preds = %22
  unreachable
}

declare i32 @printf(ptr noundef, ...) #1

declare i32 @__gxx_personality_v0(...)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

; Function Attrs: nounwind memory(none)
declare i32 @llvm.eh.typeid.for.p0(ptr) #2

declare ptr @__cxa_begin_catch(ptr)

declare void @__cxa_end_catch()

attributes #0 = { mustprogress noinline norecurse optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nounwind memory(none) }
attributes #3 = { nounwind }
attributes #4 = { noreturn }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 21.1.0 (https://github.com/llvm/llvm-project.git 3623fe661ae35c6c80ac221f14d85be76aa870f1)"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}

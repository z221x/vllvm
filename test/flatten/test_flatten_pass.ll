; ModuleID = './test_flatten.ll'
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
@0 = global i32 0

; Function Attrs: mustprogress noinline norecurse optnone uwtable
define dso_local noundef i32 @main() #0 personality ptr @__gxx_personality_v0 {
  %.reg2mem23 = alloca i32, align 4
  %.reg2mem = alloca i32, align 4
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
  store i32 %7, ptr %.reg2mem, align 4
  store i32 248759376, ptr @0, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 -1932314831, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %57, %46, %39, %36, %32, %28, %LeafBlock, %LeafBlock1, %NodeBlock, %18, %15, %11, %first, %switchLoopEnd, %0
  %loadGlobalSwitchVar = load i32, ptr @0, align 4
  br label %NodeBlock99

NodeBlock99:                                      ; preds = %switchLoopEntry
  %Pivot100 = icmp slt i32 %loadGlobalSwitchVar, 958921337
  br i1 %Pivot100, label %NodeBlock59, label %NodeBlock97

NodeBlock97:                                      ; preds = %NodeBlock99
  %Pivot98 = icmp slt i32 %loadGlobalSwitchVar, 1464299374
  br i1 %Pivot98, label %NodeBlock77, label %NodeBlock95

NodeBlock95:                                      ; preds = %NodeBlock97
  %Pivot96 = icmp slt i32 %loadGlobalSwitchVar, 1766391291
  br i1 %Pivot96, label %NodeBlock83, label %NodeBlock93

NodeBlock93:                                      ; preds = %NodeBlock95
  %Pivot94 = icmp slt i32 %loadGlobalSwitchVar, 1829888140
  br i1 %Pivot94, label %LeafBlock85, label %NodeBlock91

NodeBlock91:                                      ; preds = %NodeBlock93
  %Pivot92 = icmp slt i32 %loadGlobalSwitchVar, 1900259618
  br i1 %Pivot92, label %LeafBlock87, label %LeafBlock89

LeafBlock89:                                      ; preds = %NodeBlock91
  %SwitchLeaf90 = icmp eq i32 %loadGlobalSwitchVar, 1900259618
  br i1 %SwitchLeaf90, label %36, label %switchDefault

LeafBlock87:                                      ; preds = %NodeBlock91
  %SwitchLeaf88 = icmp eq i32 %loadGlobalSwitchVar, 1829888140
  br i1 %SwitchLeaf88, label %44, label %switchDefault

LeafBlock85:                                      ; preds = %NodeBlock93
  %SwitchLeaf86 = icmp eq i32 %loadGlobalSwitchVar, 1766391291
  br i1 %SwitchLeaf86, label %15, label %switchDefault

NodeBlock83:                                      ; preds = %NodeBlock95
  %Pivot84 = icmp slt i32 %loadGlobalSwitchVar, 1754223953
  br i1 %Pivot84, label %LeafBlock79, label %LeafBlock81

LeafBlock81:                                      ; preds = %NodeBlock83
  %SwitchLeaf82 = icmp eq i32 %loadGlobalSwitchVar, 1754223953
  br i1 %SwitchLeaf82, label %46, label %switchDefault

LeafBlock79:                                      ; preds = %NodeBlock83
  %SwitchLeaf80 = icmp eq i32 %loadGlobalSwitchVar, 1464299374
  br i1 %SwitchLeaf80, label %NodeBlock, label %switchDefault

NodeBlock77:                                      ; preds = %NodeBlock97
  %Pivot78 = icmp slt i32 %loadGlobalSwitchVar, 1220767752
  br i1 %Pivot78, label %NodeBlock65, label %NodeBlock75

NodeBlock75:                                      ; preds = %NodeBlock77
  %Pivot76 = icmp slt i32 %loadGlobalSwitchVar, 1314885733
  br i1 %Pivot76, label %LeafBlock67, label %NodeBlock73

NodeBlock73:                                      ; preds = %NodeBlock75
  %Pivot74 = icmp slt i32 %loadGlobalSwitchVar, 1380528529
  br i1 %Pivot74, label %LeafBlock69, label %LeafBlock71

LeafBlock71:                                      ; preds = %NodeBlock73
  %SwitchLeaf72 = icmp eq i32 %loadGlobalSwitchVar, 1380528529
  br i1 %SwitchLeaf72, label %60, label %switchDefault

LeafBlock69:                                      ; preds = %NodeBlock73
  %SwitchLeaf70 = icmp eq i32 %loadGlobalSwitchVar, 1314885733
  br i1 %SwitchLeaf70, label %61, label %switchDefault

LeafBlock67:                                      ; preds = %NodeBlock75
  %SwitchLeaf68 = icmp eq i32 %loadGlobalSwitchVar, 1220767752
  br i1 %SwitchLeaf68, label %57, label %switchDefault

NodeBlock65:                                      ; preds = %NodeBlock77
  %Pivot66 = icmp slt i32 %loadGlobalSwitchVar, 1163948819
  br i1 %Pivot66, label %LeafBlock61, label %LeafBlock63

LeafBlock63:                                      ; preds = %NodeBlock65
  %SwitchLeaf64 = icmp eq i32 %loadGlobalSwitchVar, 1163948819
  br i1 %SwitchLeaf64, label %LeafBlock1, label %switchDefault

LeafBlock61:                                      ; preds = %NodeBlock65
  %SwitchLeaf62 = icmp eq i32 %loadGlobalSwitchVar, 958921337
  br i1 %SwitchLeaf62, label %52, label %switchDefault

NodeBlock59:                                      ; preds = %NodeBlock99
  %Pivot60 = icmp slt i32 %loadGlobalSwitchVar, 482809744
  br i1 %Pivot60, label %NodeBlock39, label %NodeBlock57

NodeBlock57:                                      ; preds = %NodeBlock59
  %Pivot58 = icmp slt i32 %loadGlobalSwitchVar, 622288237
  br i1 %Pivot58, label %NodeBlock45, label %NodeBlock55

NodeBlock55:                                      ; preds = %NodeBlock57
  %Pivot56 = icmp slt i32 %loadGlobalSwitchVar, 701407503
  br i1 %Pivot56, label %LeafBlock47, label %NodeBlock53

NodeBlock53:                                      ; preds = %NodeBlock55
  %Pivot54 = icmp slt i32 %loadGlobalSwitchVar, 941769037
  br i1 %Pivot54, label %LeafBlock49, label %LeafBlock51

LeafBlock51:                                      ; preds = %NodeBlock53
  %SwitchLeaf52 = icmp eq i32 %loadGlobalSwitchVar, 941769037
  br i1 %SwitchLeaf52, label %28, label %switchDefault

LeafBlock49:                                      ; preds = %NodeBlock53
  %SwitchLeaf50 = icmp eq i32 %loadGlobalSwitchVar, 701407503
  br i1 %SwitchLeaf50, label %39, label %switchDefault

LeafBlock47:                                      ; preds = %NodeBlock55
  %SwitchLeaf48 = icmp eq i32 %loadGlobalSwitchVar, 622288237
  br i1 %SwitchLeaf48, label %LeafBlock, label %switchDefault

NodeBlock45:                                      ; preds = %NodeBlock57
  %Pivot46 = icmp slt i32 %loadGlobalSwitchVar, 615418864
  br i1 %Pivot46, label %LeafBlock41, label %LeafBlock43

LeafBlock43:                                      ; preds = %NodeBlock45
  %SwitchLeaf44 = icmp eq i32 %loadGlobalSwitchVar, 615418864
  br i1 %SwitchLeaf44, label %66, label %switchDefault

LeafBlock41:                                      ; preds = %NodeBlock45
  %SwitchLeaf42 = icmp eq i32 %loadGlobalSwitchVar, 482809744
  br i1 %SwitchLeaf42, label %32, label %switchDefault

NodeBlock39:                                      ; preds = %NodeBlock59
  %Pivot40 = icmp slt i32 %loadGlobalSwitchVar, 248759376
  br i1 %Pivot40, label %NodeBlock31, label %NodeBlock37

NodeBlock37:                                      ; preds = %NodeBlock39
  %Pivot38 = icmp slt i32 %loadGlobalSwitchVar, 321195379
  br i1 %Pivot38, label %LeafBlock33, label %LeafBlock35

LeafBlock35:                                      ; preds = %NodeBlock37
  %SwitchLeaf36 = icmp eq i32 %loadGlobalSwitchVar, 321195379
  br i1 %SwitchLeaf36, label %11, label %switchDefault

LeafBlock33:                                      ; preds = %NodeBlock37
  %SwitchLeaf34 = icmp eq i32 %loadGlobalSwitchVar, 248759376
  br i1 %SwitchLeaf34, label %first, label %switchDefault

NodeBlock31:                                      ; preds = %NodeBlock39
  %Pivot32 = icmp slt i32 %loadGlobalSwitchVar, 154464637
  br i1 %Pivot32, label %LeafBlock27, label %LeafBlock29

LeafBlock29:                                      ; preds = %NodeBlock31
  %SwitchLeaf30 = icmp eq i32 %loadGlobalSwitchVar, 154464637
  br i1 %SwitchLeaf30, label %42, label %switchDefault

LeafBlock27:                                      ; preds = %NodeBlock31
  %SwitchLeaf28 = icmp eq i32 %loadGlobalSwitchVar, 148219865
  br i1 %SwitchLeaf28, label %18, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock89, %LeafBlock87, %LeafBlock85, %LeafBlock81, %LeafBlock79, %LeafBlock71, %LeafBlock69, %LeafBlock67, %LeafBlock63, %LeafBlock61, %LeafBlock51, %LeafBlock49, %LeafBlock47, %LeafBlock43, %LeafBlock41, %LeafBlock35, %LeafBlock33, %LeafBlock29, %LeafBlock27
  br label %switchLoopEnd

first:                                            ; preds = %LeafBlock33
  %.reload = load i32, ptr %.reg2mem, align 4
  %8 = icmp ne i32 %.reload, 0
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %9 = xor i32 1191922707, %caseKeyTmp
  store i32 %9, ptr %caseKeyPtr, align 4
  %newNumCaseTrue = sub i32 %9, -1196200529
  %newNumCaseFalse = sub i32 %9, 1653570855
  %10 = select i1 %8, i32 %newNumCaseTrue, i32 %newNumCaseFalse
  store i32 %10, ptr @0, align 4
  br label %switchLoopEntry

11:                                               ; preds = %LeafBlock35
  %12 = call i32 (ptr, ...) @printf(ptr noundef @.str.1)
  %caseKeyTmp3 = load i32, ptr %caseKeyPtr, align 4
  %13 = xor i32 1675755572, %caseKeyTmp3
  store i32 %13, ptr %caseKeyPtr, align 4
  %14 = sub i32 %13, 1056013595
  store i32 %14, ptr @0, align 4
  br label %switchLoopEntry

15:                                               ; preds = %LeafBlock85
  %caseKeyTmp4 = load i32, ptr %caseKeyPtr, align 4
  %16 = xor i32 -219816825, %caseKeyTmp4
  store i32 %16, ptr %caseKeyPtr, align 4
  %17 = sub i32 %16, 1376387000
  store i32 %17, ptr @0, align 4
  br label %switchLoopEntry

18:                                               ; preds = %LeafBlock27
  %19 = load i32, ptr %2, align 4
  store i32 %19, ptr %.reg2mem23, align 4
  %caseKeyTmp5 = load i32, ptr %caseKeyPtr, align 4
  %20 = xor i32 1375953251, %caseKeyTmp5
  store i32 %20, ptr %caseKeyPtr, align 4
  %21 = sub i32 %20, -1315612796
  store i32 %21, ptr @0, align 4
  br label %switchLoopEntry

NodeBlock:                                        ; preds = %LeafBlock79
  %.reload26 = load i32, ptr %.reg2mem23, align 4
  %Pivot = icmp slt i32 %.reload26, 3
  %caseKeyTmp6 = load i32, ptr %caseKeyPtr, align 4
  %22 = xor i32 -1049235315, %caseKeyTmp6
  store i32 %22, ptr %caseKeyPtr, align 4
  %newNumCaseTrue7 = sub i32 %22, -1533945582
  %newNumCaseFalse8 = sub i32 %22, -2075606164
  %23 = select i1 %Pivot, i32 %newNumCaseTrue7, i32 %newNumCaseFalse8
  store i32 %23, ptr @0, align 4
  br label %switchLoopEntry

LeafBlock1:                                       ; preds = %LeafBlock63
  %.reload24 = load i32, ptr %.reg2mem23, align 4
  %SwitchLeaf2 = icmp eq i32 %.reload24, 3
  %caseKeyTmp9 = load i32, ptr %caseKeyPtr, align 4
  %24 = xor i32 -148739285, %caseKeyTmp9
  store i32 %24, ptr %caseKeyPtr, align 4
  %newNumCaseTrue10 = sub i32 %24, 566511044
  %newNumCaseFalse11 = sub i32 %24, -850938830
  %25 = select i1 %SwitchLeaf2, i32 %newNumCaseTrue10, i32 %newNumCaseFalse11
  store i32 %25, ptr @0, align 4
  br label %switchLoopEntry

LeafBlock:                                        ; preds = %LeafBlock47
  %.reload25 = load i32, ptr %.reg2mem23, align 4
  %SwitchLeaf = icmp eq i32 %.reload25, 2
  %caseKeyTmp12 = load i32, ptr %caseKeyPtr, align 4
  %26 = xor i32 698633835, %caseKeyTmp12
  store i32 %26, ptr %caseKeyPtr, align 4
  %newNumCaseTrue13 = sub i32 %26, -552807950
  %newNumCaseFalse14 = sub i32 %26, -1511298531
  %27 = select i1 %SwitchLeaf, i32 %newNumCaseTrue13, i32 %newNumCaseFalse14
  store i32 %27, ptr @0, align 4
  br label %switchLoopEntry

28:                                               ; preds = %LeafBlock51
  %29 = call i32 (ptr, ...) @printf(ptr noundef @.str.2)
  store i32 3, ptr %2, align 4
  %caseKeyTmp15 = load i32, ptr %caseKeyPtr, align 4
  %30 = xor i32 -477704683, %caseKeyTmp15
  store i32 %30, ptr %caseKeyPtr, align 4
  %31 = sub i32 %30, -891601893
  store i32 %31, ptr @0, align 4
  br label %switchLoopEntry

32:                                               ; preds = %LeafBlock41
  %33 = call i32 (ptr, ...) @printf(ptr noundef @.str.3)
  %caseKeyTmp16 = load i32, ptr %caseKeyPtr, align 4
  %34 = xor i32 -1150560955, %caseKeyTmp16
  store i32 %34, ptr %caseKeyPtr, align 4
  %35 = sub i32 %34, 1183651570
  store i32 %35, ptr @0, align 4
  br label %switchLoopEntry

36:                                               ; preds = %LeafBlock89
  %caseKeyTmp17 = load i32, ptr %caseKeyPtr, align 4
  %37 = xor i32 -2124619230, %caseKeyTmp17
  store i32 %37, ptr %caseKeyPtr, align 4
  %38 = sub i32 %37, -1529853634
  store i32 %38, ptr @0, align 4
  br label %switchLoopEntry

39:                                               ; preds = %LeafBlock49
  %caseKeyTmp18 = load i32, ptr %caseKeyPtr, align 4
  %40 = xor i32 -2029295983, %caseKeyTmp18
  store i32 %40, ptr %caseKeyPtr, align 4
  %41 = sub i32 %40, 1086317827
  store i32 %41, ptr @0, align 4
  br label %switchLoopEntry

42:                                               ; preds = %LeafBlock29
  %43 = invoke i32 (ptr, ...) @printf(ptr noundef @.str.4)
          to label %44 unwind label %67

44:                                               ; preds = %LeafBlock87, %42
  %45 = call ptr @__cxa_allocate_exception(i64 8) #3
  store ptr @.str.5, ptr %45, align 16
  invoke void @__cxa_throw(ptr %45, ptr @_ZTIPKc, ptr null) #4
          to label %66 unwind label %67

46:                                               ; preds = %LeafBlock81, %67
  %47 = load i32, ptr %4, align 4
  %48 = call i32 @llvm.eh.typeid.for.p0(ptr @_ZTIPKc) #3
  %49 = icmp eq i32 %47, %48
  %caseKeyTmp19 = load i32, ptr %caseKeyPtr, align 4
  %50 = xor i32 228072189, %caseKeyTmp19
  store i32 %50, ptr %caseKeyPtr, align 4
  %newNumCaseTrue20 = sub i32 %50, 182819752
  %newNumCaseFalse21 = sub i32 %50, -173144644
  %51 = select i1 %49, i32 %newNumCaseTrue20, i32 %newNumCaseFalse21
  store i32 %51, ptr @0, align 4
  br label %switchLoopEntry

52:                                               ; preds = %LeafBlock61
  %53 = load ptr, ptr %3, align 8
  %54 = call ptr @__cxa_begin_catch(ptr %53) #3
  store ptr %54, ptr %5, align 8
  %55 = load ptr, ptr %5, align 8
  %56 = invoke i32 (ptr, ...) @printf(ptr noundef @.str.6, ptr noundef %55)
          to label %57 unwind label %71

57:                                               ; preds = %LeafBlock67, %52
  call void @__cxa_end_catch() #3
  %caseKeyTmp22 = load i32, ptr %caseKeyPtr, align 4
  %58 = xor i32 1752371478, %caseKeyTmp22
  store i32 %58, ptr %caseKeyPtr, align 4
  %59 = sub i32 %58, -634038874
  store i32 %59, ptr @0, align 4
  br label %switchLoopEntry

60:                                               ; preds = %LeafBlock71
  ret i32 0

61:                                               ; preds = %LeafBlock69, %71
  %62 = load ptr, ptr %3, align 8
  %63 = load i32, ptr %4, align 4
  %64 = insertvalue { ptr, i32 } poison, ptr %62, 0
  %65 = insertvalue { ptr, i32 } %64, i32 %63, 1
  resume { ptr, i32 } %65

66:                                               ; preds = %LeafBlock43, %44
  unreachable

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry

67:                                               ; preds = %44, %42
  %68 = landingpad { ptr, i32 }
          catch ptr @_ZTIPKc
  %69 = extractvalue { ptr, i32 } %68, 0
  store ptr %69, ptr %3, align 8
  %70 = extractvalue { ptr, i32 } %68, 1
  store i32 %70, ptr %4, align 4
  br label %46

71:                                               ; preds = %52
  %72 = landingpad { ptr, i32 }
          cleanup
  %73 = extractvalue { ptr, i32 } %72, 0
  store ptr %73, ptr %3, align 8
  %74 = extractvalue { ptr, i32 } %72, 1
  store i32 %74, ptr %4, align 4
  call void @__cxa_end_catch() #3
  br label %61
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

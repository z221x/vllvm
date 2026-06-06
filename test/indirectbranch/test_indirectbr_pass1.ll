; ModuleID = './test_indirectbr_pass.ll'
source_filename = "./test_indirectbr.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr global [15 x i8] c"\EA\1B\84\AC!\F1V\92\AF@%\A2\ED`\F9", align 1
@.str.1 = private unnamed_addr global [5 x i8] c"\D4\B0\BB\0D\16", align 1
@.str.2 = private unnamed_addr global [5 x i8] c"\F5\A9\09\CE\F6", align 1
@.str.3 = private unnamed_addr global [5 x i8] c"\E0;\86\C0\C0", align 1
@.str.4 = private unnamed_addr global [5 x i8] c"*\90R\93\02", align 1
@.str.5 = private unnamed_addr global [15 x i8] c"B}\7F\E4\D0\CBc\0E\80\E5\DC{\B3e\12", align 1
@.str.6 = private unnamed_addr global [15 x i8] c"\16\818\0Ae\05\F5\A9`'K\A8\B17\16", align 1
@.str.7 = private unnamed_addr global [15 x i8] c"\CC\B7'\9AF\8Eru\FBcri~\BD\FF", align 1
@.str.8 = private unnamed_addr global [15 x i8] c"\94\A49|\A3r9\A73\BA\DB\CEG\C1\B4", align 1
@.str.9 = private unnamed_addr global [15 x i8] c"\8E\91v\85\C0\F8\AB\B4\AD\94\CB\9A\81\BE\D3", align 1
@func_tablefuncE = private constant [0 x ptr] zeroinitializer
@0 = private global [0 x ptr] zeroinitializer
@func_tablefuncF = private constant [1 x ptr] [ptr getelementptr (ptr, ptr @_decrypto1, i32 -37853728)]
@1 = private global [0 x ptr] zeroinitializer
@func_tablemain = private constant [11 x ptr] [ptr getelementptr (ptr, ptr @_decrypto8, i32 886394097), ptr getelementptr (ptr, ptr @_decrypto4, i32 651950210), ptr getelementptr (ptr, ptr @_decrypto2, i32 1426868262), ptr getelementptr (ptr, ptr @_decrypto5, i32 1518495621), ptr getelementptr (ptr, ptr @_decrypto9, i32 1007100072), ptr getelementptr (ptr, ptr @_decrypto7, i32 1444086318), ptr getelementptr (ptr, ptr @_decrypto6, i32 1209832223), ptr getelementptr (ptr, ptr @_decrypto10, i32 179295370), ptr getelementptr (ptr, ptr @funcF, i32 1118918807), ptr getelementptr (ptr, ptr @funcE, i32 1964685448), ptr getelementptr (ptr, ptr @_decrypto3, i32 2000807204)]
@2 = private global [8 x ptr] [ptr getelementptr (i8, ptr blockaddress(@main, %43), i32 -1440974030), ptr getelementptr (i8, ptr blockaddress(@main, %8), i32 -72585067), ptr getelementptr (i8, ptr blockaddress(@main, %24), i32 -1264044431), ptr getelementptr (i8, ptr blockaddress(@main, %99), i32 -985475229), ptr getelementptr (i8, ptr blockaddress(@main, %57), i32 -454661462), ptr getelementptr (i8, ptr blockaddress(@main, %16), i32 -1732104339), ptr getelementptr (i8, ptr blockaddress(@main, %49), i32 -72547805), ptr getelementptr (i8, ptr blockaddress(@main, %32), i32 -997862146)]
@func_table_decrypto1 = private constant [0 x ptr] zeroinitializer
@3 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto1, %loopbr), i32 -488788981), ptr getelementptr (i8, ptr blockaddress(@_decrypto1, %exit), i32 -1907696845), ptr getelementptr (i8, ptr blockaddress(@_decrypto1, %loop), i32 -2106658884)]
@func_table_decrypto2 = private constant [0 x ptr] zeroinitializer
@4 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto2, %loopbr), i32 434210216), ptr getelementptr (i8, ptr blockaddress(@_decrypto2, %exit), i32 190184531), ptr getelementptr (i8, ptr blockaddress(@_decrypto2, %loop), i32 669224681)]
@func_table_decrypto3 = private constant [0 x ptr] zeroinitializer
@5 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto3, %loopbr), i32 698320150), ptr getelementptr (i8, ptr blockaddress(@_decrypto3, %exit), i32 694123988), ptr getelementptr (i8, ptr blockaddress(@_decrypto3, %loop), i32 916022158)]
@func_table_decrypto4 = private constant [0 x ptr] zeroinitializer
@6 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto4, %loopbr), i32 234552558), ptr getelementptr (i8, ptr blockaddress(@_decrypto4, %exit), i32 1008777480), ptr getelementptr (i8, ptr blockaddress(@_decrypto4, %loop), i32 1576707421)]
@func_table_decrypto5 = private constant [0 x ptr] zeroinitializer
@7 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto5, %loopbr), i32 -639621003), ptr getelementptr (i8, ptr blockaddress(@_decrypto5, %exit), i32 -621830136), ptr getelementptr (i8, ptr blockaddress(@_decrypto5, %loop), i32 -1366033423)]
@func_table_decrypto6 = private constant [0 x ptr] zeroinitializer
@8 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto6, %loopbr), i32 1839882907), ptr getelementptr (i8, ptr blockaddress(@_decrypto6, %exit), i32 1903998004), ptr getelementptr (i8, ptr blockaddress(@_decrypto6, %loop), i32 93448112)]
@func_table_decrypto7 = private constant [0 x ptr] zeroinitializer
@9 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto7, %loopbr), i32 -1810331008), ptr getelementptr (i8, ptr blockaddress(@_decrypto7, %exit), i32 -1108425792), ptr getelementptr (i8, ptr blockaddress(@_decrypto7, %loop), i32 -103213734)]
@func_table_decrypto8 = private constant [0 x ptr] zeroinitializer
@10 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto8, %loopbr), i32 1497723104), ptr getelementptr (i8, ptr blockaddress(@_decrypto8, %exit), i32 406983335), ptr getelementptr (i8, ptr blockaddress(@_decrypto8, %loop), i32 1994788796)]
@func_table_decrypto9 = private constant [0 x ptr] zeroinitializer
@11 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto9, %loopbr), i32 1714106327), ptr getelementptr (i8, ptr blockaddress(@_decrypto9, %exit), i32 36573602), ptr getelementptr (i8, ptr blockaddress(@_decrypto9, %loop), i32 1711381463)]
@func_table_decrypto10 = private constant [0 x ptr] zeroinitializer
@12 = private global [3 x ptr] [ptr getelementptr (i8, ptr blockaddress(@_decrypto10, %loopbr), i32 -1667879518), ptr getelementptr (i8, ptr blockaddress(@_decrypto10, %exit), i32 -721919326), ptr getelementptr (i8, ptr blockaddress(@_decrypto10, %loop), i32 -831366031)]
@13 = global i32 0
@14 = global i32 0
@15 = global i32 0
@16 = global i32 0
@17 = global i32 0
@18 = global i32 0
@19 = global i32 0
@20 = global i32 0
@21 = global i32 0
@22 = global i32 0
@23 = global i32 0
@24 = global i32 0
@25 = global i32 0

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @funcE() #0 {
  ret i32 1
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @funcF() #0 {
  %1 = load ptr, ptr @func_tablefuncF, align 8
  %2 = getelementptr i64, ptr %1, i32 37853728
  %3 = call ptr %2(ptr @.str)
  %4 = call i32 (ptr, ...) @printf(ptr noundef %3)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %.reg2mem = alloca i32, align 4
  %1 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  %2 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 2), align 8
  %3 = getelementptr i64, ptr %2, i32 -1426868262
  %4 = call ptr %3(ptr @.str.1)
  %5 = call i32 (ptr, ...) @printf(ptr noundef %4)
  %6 = load ptr, ptr getelementptr (i64, ptr @2, i64 1), align 8
  %7 = getelementptr i8, ptr %6, i32 72585067
  store i32 -258727582, ptr @15, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %91, %83, %75, %67, %LeafBlock, %NodeBlock, %LeafBlock1, %NodeBlock3, %NodeBlock5, %57, %49, %43, %24, %16, %8, %switchLoopEnd, %0
  %loadGlobalSwitchVar = load i32, ptr @15, align 4
  br label %NodeBlock97

NodeBlock97:                                      ; preds = %switchLoopEntry
  %Pivot98 = icmp slt i32 %loadGlobalSwitchVar, -675292163
  br i1 %Pivot98, label %NodeBlock61, label %NodeBlock95

NodeBlock95:                                      ; preds = %NodeBlock97
  %Pivot96 = icmp slt i32 %loadGlobalSwitchVar, -258727582
  br i1 %Pivot96, label %NodeBlock75, label %NodeBlock93

NodeBlock93:                                      ; preds = %NodeBlock95
  %Pivot94 = icmp slt i32 %loadGlobalSwitchVar, -168275958
  br i1 %Pivot94, label %NodeBlock81, label %NodeBlock91

NodeBlock91:                                      ; preds = %NodeBlock93
  %Pivot92 = icmp slt i32 %loadGlobalSwitchVar, -138774682
  br i1 %Pivot92, label %LeafBlock83, label %NodeBlock89

NodeBlock89:                                      ; preds = %NodeBlock91
  %Pivot90 = icmp slt i32 %loadGlobalSwitchVar, -30503859
  br i1 %Pivot90, label %LeafBlock85, label %LeafBlock87

LeafBlock87:                                      ; preds = %NodeBlock89
  %SwitchLeaf88 = icmp eq i32 %loadGlobalSwitchVar, -30503859
  br i1 %SwitchLeaf88, label %16, label %switchDefault

LeafBlock85:                                      ; preds = %NodeBlock89
  %SwitchLeaf86 = icmp eq i32 %loadGlobalSwitchVar, -138774682
  br i1 %SwitchLeaf86, label %91, label %switchDefault

LeafBlock83:                                      ; preds = %NodeBlock91
  %SwitchLeaf84 = icmp eq i32 %loadGlobalSwitchVar, -168275958
  br i1 %SwitchLeaf84, label %49, label %switchDefault

NodeBlock81:                                      ; preds = %NodeBlock93
  %Pivot82 = icmp slt i32 %loadGlobalSwitchVar, -191029884
  br i1 %Pivot82, label %LeafBlock77, label %LeafBlock79

LeafBlock79:                                      ; preds = %NodeBlock81
  %SwitchLeaf80 = icmp eq i32 %loadGlobalSwitchVar, -191029884
  br i1 %SwitchLeaf80, label %LeafBlock, label %switchDefault

LeafBlock77:                                      ; preds = %NodeBlock81
  %SwitchLeaf78 = icmp eq i32 %loadGlobalSwitchVar, -258727582
  br i1 %SwitchLeaf78, label %8, label %switchDefault

NodeBlock75:                                      ; preds = %NodeBlock95
  %Pivot76 = icmp slt i32 %loadGlobalSwitchVar, -417265431
  br i1 %Pivot76, label %NodeBlock67, label %NodeBlock73

NodeBlock73:                                      ; preds = %NodeBlock75
  %Pivot74 = icmp slt i32 %loadGlobalSwitchVar, -416652330
  br i1 %Pivot74, label %LeafBlock69, label %LeafBlock71

LeafBlock71:                                      ; preds = %NodeBlock73
  %SwitchLeaf72 = icmp eq i32 %loadGlobalSwitchVar, -416652330
  br i1 %SwitchLeaf72, label %43, label %switchDefault

LeafBlock69:                                      ; preds = %NodeBlock73
  %SwitchLeaf70 = icmp eq i32 %loadGlobalSwitchVar, -417265431
  br i1 %SwitchLeaf70, label %32, label %switchDefault

NodeBlock67:                                      ; preds = %NodeBlock75
  %Pivot68 = icmp slt i32 %loadGlobalSwitchVar, -458722245
  br i1 %Pivot68, label %LeafBlock63, label %LeafBlock65

LeafBlock65:                                      ; preds = %NodeBlock67
  %SwitchLeaf66 = icmp eq i32 %loadGlobalSwitchVar, -458722245
  br i1 %SwitchLeaf66, label %NodeBlock, label %switchDefault

LeafBlock63:                                      ; preds = %NodeBlock67
  %SwitchLeaf64 = icmp eq i32 %loadGlobalSwitchVar, -675292163
  br i1 %SwitchLeaf64, label %LeafBlock1, label %switchDefault

NodeBlock61:                                      ; preds = %NodeBlock97
  %Pivot62 = icmp slt i32 %loadGlobalSwitchVar, -1690370460
  br i1 %Pivot62, label %NodeBlock45, label %NodeBlock59

NodeBlock59:                                      ; preds = %NodeBlock61
  %Pivot60 = icmp slt i32 %loadGlobalSwitchVar, -1330314266
  br i1 %Pivot60, label %NodeBlock51, label %NodeBlock57

NodeBlock57:                                      ; preds = %NodeBlock59
  %Pivot58 = icmp slt i32 %loadGlobalSwitchVar, -1199797792
  br i1 %Pivot58, label %LeafBlock53, label %LeafBlock55

LeafBlock55:                                      ; preds = %NodeBlock57
  %SwitchLeaf56 = icmp eq i32 %loadGlobalSwitchVar, -1199797792
  br i1 %SwitchLeaf56, label %NodeBlock5, label %switchDefault

LeafBlock53:                                      ; preds = %NodeBlock57
  %SwitchLeaf54 = icmp eq i32 %loadGlobalSwitchVar, -1330314266
  br i1 %SwitchLeaf54, label %57, label %switchDefault

NodeBlock51:                                      ; preds = %NodeBlock59
  %Pivot52 = icmp slt i32 %loadGlobalSwitchVar, -1333811354
  br i1 %Pivot52, label %LeafBlock47, label %LeafBlock49

LeafBlock49:                                      ; preds = %NodeBlock51
  %SwitchLeaf50 = icmp eq i32 %loadGlobalSwitchVar, -1333811354
  br i1 %SwitchLeaf50, label %75, label %switchDefault

LeafBlock47:                                      ; preds = %NodeBlock51
  %SwitchLeaf48 = icmp eq i32 %loadGlobalSwitchVar, -1690370460
  br i1 %SwitchLeaf48, label %NodeBlock3, label %switchDefault

NodeBlock45:                                      ; preds = %NodeBlock61
  %Pivot46 = icmp slt i32 %loadGlobalSwitchVar, -1744797441
  br i1 %Pivot46, label %NodeBlock37, label %NodeBlock43

NodeBlock43:                                      ; preds = %NodeBlock45
  %Pivot44 = icmp slt i32 %loadGlobalSwitchVar, -1744702050
  br i1 %Pivot44, label %LeafBlock39, label %LeafBlock41

LeafBlock41:                                      ; preds = %NodeBlock43
  %SwitchLeaf42 = icmp eq i32 %loadGlobalSwitchVar, -1744702050
  br i1 %SwitchLeaf42, label %99, label %switchDefault

LeafBlock39:                                      ; preds = %NodeBlock43
  %SwitchLeaf40 = icmp eq i32 %loadGlobalSwitchVar, -1744797441
  br i1 %SwitchLeaf40, label %83, label %switchDefault

NodeBlock37:                                      ; preds = %NodeBlock45
  %Pivot38 = icmp slt i32 %loadGlobalSwitchVar, -1851899335
  br i1 %Pivot38, label %LeafBlock33, label %LeafBlock35

LeafBlock35:                                      ; preds = %NodeBlock37
  %SwitchLeaf36 = icmp eq i32 %loadGlobalSwitchVar, -1851899335
  br i1 %SwitchLeaf36, label %24, label %switchDefault

LeafBlock33:                                      ; preds = %NodeBlock37
  %SwitchLeaf34 = icmp eq i32 %loadGlobalSwitchVar, -1959930672
  br i1 %SwitchLeaf34, label %67, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock87, %LeafBlock85, %LeafBlock83, %LeafBlock79, %LeafBlock77, %LeafBlock71, %LeafBlock69, %LeafBlock65, %LeafBlock63, %LeafBlock55, %LeafBlock53, %LeafBlock49, %LeafBlock47, %LeafBlock41, %LeafBlock39, %LeafBlock35, %LeafBlock33
  br label %switchLoopEnd

8:                                                ; preds = %LeafBlock77
  %9 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 10), align 8
  %10 = getelementptr i64, ptr %9, i32 -2000807204
  %11 = call ptr %10(ptr @.str.2)
  %12 = call i32 (ptr, ...) @printf(ptr noundef %11)
  %13 = load ptr, ptr getelementptr (i64, ptr @2, i64 5), align 8
  %14 = getelementptr i8, ptr %13, i32 1732104339
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %15 = sub i32 %caseKeyTmp, 1836102458
  store i32 %15, ptr @15, align 4
  br label %switchLoopEntry

16:                                               ; preds = %LeafBlock87
  %17 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 1), align 8
  %18 = getelementptr i64, ptr %17, i32 -651950210
  %19 = call ptr %18(ptr @.str.3)
  %20 = call i32 (ptr, ...) @printf(ptr noundef %19)
  %21 = load ptr, ptr getelementptr (i64, ptr @2, i64 2), align 8
  %22 = getelementptr i8, ptr %21, i32 1264044431
  %caseKeyTmp7 = load i32, ptr %caseKeyPtr, align 4
  %23 = sub i32 %caseKeyTmp7, -637469362
  store i32 %23, ptr @15, align 4
  br label %switchLoopEntry

24:                                               ; preds = %LeafBlock35
  %25 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 3), align 8
  %26 = getelementptr i64, ptr %25, i32 -1518495621
  %27 = call ptr %26(ptr @.str.4)
  %28 = call i32 (ptr, ...) @printf(ptr noundef %27)
  %29 = load ptr, ptr getelementptr (i64, ptr @2, i64 7), align 8
  %30 = getelementptr i8, ptr %29, i32 997862146
  %caseKeyTmp8 = load i32, ptr %caseKeyPtr, align 4
  %31 = sub i32 %caseKeyTmp8, -2072103266
  store i32 %31, ptr @15, align 4
  br label %switchLoopEntry

32:                                               ; preds = %LeafBlock69
  %33 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 9), align 8
  %34 = getelementptr i64, ptr %33, i32 -1964685448
  %35 = call i32 %34()
  %36 = icmp ne i32 %35, 0
  %37 = select i1 %36, i32 -1440974030, i32 -72547805
  %38 = select i1 %36, i32 0, i32 6
  %39 = getelementptr i64, ptr @2, i32 %38
  %40 = load ptr, ptr %39, align 8
  %41 = sub i32 0, %37
  %42 = getelementptr i8, ptr %40, i32 %41
  indirectbr ptr %42, [label %43, label %49]

43:                                               ; preds = %LeafBlock71, %32
  %44 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 8), align 8
  %45 = getelementptr i64, ptr %44, i32 -1118918807
  call void %45()
  %46 = load ptr, ptr getelementptr (i64, ptr @2, i64 4), align 8
  %47 = getelementptr i8, ptr %46, i32 454661462
  %caseKeyTmp9 = load i32, ptr %caseKeyPtr, align 4
  %48 = sub i32 %caseKeyTmp9, -1159054431
  store i32 %48, ptr @15, align 4
  br label %switchLoopEntry

49:                                               ; preds = %LeafBlock83, %32
  %50 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 6), align 8
  %51 = getelementptr i64, ptr %50, i32 -1209832223
  %52 = call ptr %51(ptr @.str.5)
  %53 = call i32 (ptr, ...) @printf(ptr noundef %52)
  %54 = load ptr, ptr getelementptr (i64, ptr @2, i64 4), align 8
  %55 = getelementptr i8, ptr %54, i32 454661462
  %caseKeyTmp10 = load i32, ptr %caseKeyPtr, align 4
  %56 = sub i32 %caseKeyTmp10, -1159054431
  store i32 %56, ptr @15, align 4
  br label %switchLoopEntry

57:                                               ; preds = %LeafBlock53
  %58 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 9), align 8
  %59 = getelementptr i64, ptr %58, i32 -1964685448
  %60 = call i32 %59()
  store i32 %60, ptr %.reg2mem, align 4
  %caseKeyTmp11 = load i32, ptr %caseKeyPtr, align 4
  %61 = sub i32 %caseKeyTmp11, -1289570905
  store i32 %61, ptr @15, align 4
  br label %switchLoopEntry

NodeBlock5:                                       ; preds = %LeafBlock55
  %.reload32 = load i32, ptr %.reg2mem, align 4
  %Pivot6 = icmp slt i32 %.reload32, 3
  %caseKeyTmp12 = load i32, ptr %caseKeyPtr, align 4
  %newNumCaseTrue = sub i32 %caseKeyTmp12, -2030646452
  %newNumCaseFalse = sub i32 %caseKeyTmp12, -798998237
  %62 = select i1 %Pivot6, i32 %newNumCaseTrue, i32 %newNumCaseFalse
  store i32 %62, ptr @15, align 4
  br label %switchLoopEntry

NodeBlock3:                                       ; preds = %LeafBlock47
  %.reload29 = load i32, ptr %.reg2mem, align 4
  %Pivot4 = icmp slt i32 %.reload29, 4
  %caseKeyTmp13 = load i32, ptr %caseKeyPtr, align 4
  %newNumCaseTrue14 = sub i32 %caseKeyTmp13, -744571256
  %newNumCaseFalse15 = sub i32 %caseKeyTmp13, -1814076534
  %63 = select i1 %Pivot4, i32 %newNumCaseTrue14, i32 %newNumCaseFalse15
  store i32 %63, ptr @15, align 4
  br label %switchLoopEntry

LeafBlock1:                                       ; preds = %LeafBlock63
  %.reload = load i32, ptr %.reg2mem, align 4
  %SwitchLeaf2 = icmp eq i32 %.reload, 4
  %caseKeyTmp16 = load i32, ptr %caseKeyPtr, align 4
  %newNumCaseTrue17 = sub i32 %caseKeyTmp16, 1944373281
  %newNumCaseFalse18 = sub i32 %caseKeyTmp16, -744666647
  %64 = select i1 %SwitchLeaf2, i32 %newNumCaseTrue17, i32 %newNumCaseFalse18
  store i32 %64, ptr @15, align 4
  br label %switchLoopEntry

NodeBlock:                                        ; preds = %LeafBlock65
  %.reload31 = load i32, ptr %.reg2mem, align 4
  %Pivot = icmp slt i32 %.reload31, 2
  %caseKeyTmp19 = load i32, ptr %caseKeyPtr, align 4
  %newNumCaseTrue20 = sub i32 %caseKeyTmp19, 1996628483
  %newNumCaseFalse21 = sub i32 %caseKeyTmp19, -1155557343
  %65 = select i1 %Pivot, i32 %newNumCaseTrue20, i32 %newNumCaseFalse21
  store i32 %65, ptr @15, align 4
  br label %switchLoopEntry

LeafBlock:                                        ; preds = %LeafBlock79
  %.reload30 = load i32, ptr %.reg2mem, align 4
  %SwitchLeaf = icmp eq i32 %.reload30, 1
  %caseKeyTmp22 = load i32, ptr %caseKeyPtr, align 4
  %newNumCaseTrue23 = sub i32 %caseKeyTmp22, -529438025
  %newNumCaseFalse24 = sub i32 %caseKeyTmp22, -744666647
  %66 = select i1 %SwitchLeaf, i32 %newNumCaseTrue23, i32 %newNumCaseFalse24
  store i32 %66, ptr @15, align 4
  br label %switchLoopEntry

67:                                               ; preds = %LeafBlock33
  %68 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 5), align 8
  %69 = getelementptr i64, ptr %68, i32 -1444086318
  %70 = call ptr %69(ptr @.str.6)
  %71 = call i32 (ptr, ...) @printf(ptr noundef %70)
  %72 = load ptr, ptr getelementptr (i64, ptr @2, i64 3), align 8
  %73 = getelementptr i8, ptr %72, i32 985475229
  %caseKeyTmp25 = load i32, ptr %caseKeyPtr, align 4
  %74 = sub i32 %caseKeyTmp25, -744666647
  store i32 %74, ptr @15, align 4
  br label %switchLoopEntry

75:                                               ; preds = %LeafBlock49
  %76 = load ptr, ptr @func_tablemain, align 8
  %77 = getelementptr i64, ptr %76, i32 -886394097
  %78 = call ptr %77(ptr @.str.7)
  %79 = call i32 (ptr, ...) @printf(ptr noundef %78)
  %80 = load ptr, ptr getelementptr (i64, ptr @2, i64 3), align 8
  %81 = getelementptr i8, ptr %80, i32 985475229
  %caseKeyTmp26 = load i32, ptr %caseKeyPtr, align 4
  %82 = sub i32 %caseKeyTmp26, -744666647
  store i32 %82, ptr @15, align 4
  br label %switchLoopEntry

83:                                               ; preds = %LeafBlock39
  %84 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 4), align 8
  %85 = getelementptr i64, ptr %84, i32 -1007100072
  %86 = call ptr %85(ptr @.str.8)
  %87 = call i32 (ptr, ...) @printf(ptr noundef %86)
  %88 = load ptr, ptr getelementptr (i64, ptr @2, i64 3), align 8
  %89 = getelementptr i8, ptr %88, i32 985475229
  %caseKeyTmp27 = load i32, ptr %caseKeyPtr, align 4
  %90 = sub i32 %caseKeyTmp27, -744666647
  store i32 %90, ptr @15, align 4
  br label %switchLoopEntry

91:                                               ; preds = %LeafBlock85
  %92 = load ptr, ptr getelementptr (i64, ptr @func_tablemain, i64 7), align 8
  %93 = getelementptr i64, ptr %92, i32 -179295370
  %94 = call ptr %93(ptr @.str.9)
  %95 = call i32 (ptr, ...) @printf(ptr noundef %94)
  %96 = load ptr, ptr getelementptr (i64, ptr @2, i64 3), align 8
  %97 = getelementptr i8, ptr %96, i32 985475229
  %caseKeyTmp28 = load i32, ptr %caseKeyPtr, align 4
  %98 = sub i32 %caseKeyTmp28, -744666647
  store i32 %98, ptr @15, align 4
  br label %switchLoopEntry

99:                                               ; preds = %LeafBlock41
  %100 = load i32, ptr %1, align 4
  ret i32 %100

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

declare ptr @malloc(i64)

define private ptr @_decrypto1(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 15)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -66, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 115, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 -19, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -33, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 1, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -104, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 37, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -78, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -55, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 53, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 75, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -63, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -85, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 106, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -7, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 -60, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @3, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 2106658884
  store i32 -1438338527, ptr @16, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @16, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, -1438338527
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, -849486930
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, -849486930
  br i1 %SwitchLeaf9, label %exit, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, -1438338527
  br i1 %SwitchLeaf7, label %loop, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, -1489225201
  br i1 %SwitchLeaf, label %loopbr, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock6
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 15
  %19 = select i1 %cond, i32 -488788981, i32 -1907696845
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @3, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @3, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 2106658884
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, -1051030170
  store i32 %28, ptr @16, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock8, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto2(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 5)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -106, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -14, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 -118, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 7, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 22, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -23, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -85, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -7, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -37, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 91, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 66, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 8, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -21, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 63, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -17, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 71, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @4, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 -669224681
  store i32 -1654955728, ptr @17, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @17, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, -1654955728
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, -371267034
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, -371267034
  br i1 %SwitchLeaf9, label %exit, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, -1654955728
  br i1 %SwitchLeaf7, label %loop, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, -2100478267
  br i1 %SwitchLeaf, label %loopbr, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock6
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 5
  %19 = select i1 %cond, i32 434210216, i32 190184531
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @4, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @4, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 -669224681
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, -834412969
  store i32 %28, ptr @17, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock8, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto3(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 5)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -73, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -21, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 59, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -60, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 -10, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -83, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 63, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 99, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 85, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 126, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -97, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -55, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 1, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -67, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 83, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 82, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @5, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 -916022158
  store i32 -84394063, ptr @18, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @18, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, -966949601
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, -84394063
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, -84394063
  br i1 %SwitchLeaf9, label %loop, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, -966949601
  br i1 %SwitchLeaf7, label %exit, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, -1025238568
  br i1 %SwitchLeaf, label %loopbr, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock8
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 5
  %19 = select i1 %cond, i32 698320150, i32 694123988
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @5, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @5, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 -916022158
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, 1889992662
  store i32 %28, ptr @18, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock6, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto4(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 5)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -94, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 121, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 -75, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -54, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 -64, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -121, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -66, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 30, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -28, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 76, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -24, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -83, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -101, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -108, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -124, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 -74, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @6, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 -1576707421
  store i32 671420601, ptr @19, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @19, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, 1586175456
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, 1720663341
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, 1720663341
  br i1 %SwitchLeaf9, label %loopbr, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, 1586175456
  br i1 %SwitchLeaf7, label %exit, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, 671420601
  br i1 %SwitchLeaf, label %loop, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 5
  %19 = select i1 %cond, i32 234552558, i32 1008777480
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @6, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock8, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @6, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 -1576707421
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, 1134177998
  store i32 %28, ptr @19, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock6, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto5(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 5)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 104, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -46, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 102, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -103, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 2, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 53, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -17, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 124, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -123, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 49, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -117, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -109, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -11, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -78, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -40, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 93, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @7, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 1366033423
  store i32 2109700140, ptr @20, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @20, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, 1390813778
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, 2109700140
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, 2109700140
  br i1 %SwitchLeaf9, label %loop, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, 1390813778
  br i1 %SwitchLeaf7, label %exit, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, 285015780
  br i1 %SwitchLeaf, label %loopbr, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock8
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 5
  %19 = select i1 %cond, i32 -639621003, i32 -621830136
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @7, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @7, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 1366033423
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, -304101541
  store i32 %28, ptr @20, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock6, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto6(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 15)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 22, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 21, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 22, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -105, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 -16, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -94, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 16, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 46, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -26, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 -112, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -78, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 24, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -2, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 111, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 18, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 45, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @8, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 -93448112
  store i32 -1243347927, ptr @21, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @21, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, -1243347927
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, -914581097
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, -914581097
  br i1 %SwitchLeaf9, label %exit, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, -1243347927
  br i1 %SwitchLeaf7, label %loop, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, -2137229286
  br i1 %SwitchLeaf, label %loopbr, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock6
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 15
  %19 = select i1 %cond, i32 1839882907, i32 1903998004
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @8, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @8, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 -93448112
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, -1246020770
  store i32 %28, ptr @21, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock8, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto7(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 15)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 66, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -23, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 81, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 121, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 69, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 108, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -122, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -119, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 6, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 82, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 37, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -53, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -128, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 61, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 22, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 38, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @9, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 103213734
  store i32 1838012845, ptr @22, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @22, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, 1838012845
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, 1910924539
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, 1910924539
  br i1 %SwitchLeaf9, label %loopbr, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, 1838012845
  br i1 %SwitchLeaf7, label %loop, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, 1025589639
  br i1 %SwitchLeaf, label %exit, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock6
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 15
  %19 = select i1 %cond, i32 -1810331008, i32 -1108425792
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @9, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock8, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @9, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 103213734
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, -32414246
  store i32 %28, ptr @22, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto8(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 15)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -104, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -33, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 78, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -23, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 102, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -25, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 1, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 85, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -99, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 22, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 28, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 10, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 76, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -73, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -1, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 -72, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @10, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 -1994788796
  store i32 -687046980, ptr @23, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @23, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, -332577349
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, -122179071
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, -122179071
  br i1 %SwitchLeaf9, label %loopbr, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, -332577349
  br i1 %SwitchLeaf7, label %exit, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, -687046980
  br i1 %SwitchLeaf, label %loop, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 15
  %19 = select i1 %cond, i32 1497723104, i32 406983335
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @10, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock8, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @10, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 -1994788796
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, -1802321717
  store i32 %28, ptr @23, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock6, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto9(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 15)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -64, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -52, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 80, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 15, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 -125, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 27, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 74, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -121, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 85, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 -49, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -75, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -83, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 116, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -53, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -76, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 -113, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @11, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 -1711381463
  store i32 1747397988, ptr @24, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @24, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, 1747397988
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, 1754248642
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, 1754248642
  br i1 %SwitchLeaf9, label %exit, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, 1747397988
  br i1 %SwitchLeaf7, label %loop, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, 815719632
  br i1 %SwitchLeaf, label %loopbr, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock6
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 15
  %19 = select i1 %cond, i32 1714106327, i32 36573602
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @11, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @11, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 -1711381463
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, 58200611
  store i32 %28, ptr @24, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock8, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
}

define private ptr @_decrypto10(ptr %strArg) {
entry:
  %iLoad.reg2mem = alloca i64, align 8
  %.reg2mem = alloca ptr, align 8
  %0 = call ptr @malloc(i64 15)
  store ptr %0, ptr %.reg2mem, align 8
  %key = alloca [16 x i8], align 1
  %1 = getelementptr [16 x i8], ptr %key, i32 0, i32 0
  store i8 -38, ptr %1, align 1
  %2 = getelementptr [16 x i8], ptr %key, i32 0, i32 1
  store i8 -7, ptr %2, align 1
  %3 = getelementptr [16 x i8], ptr %key, i32 0, i32 2
  store i8 31, ptr %3, align 1
  %4 = getelementptr [16 x i8], ptr %key, i32 0, i32 3
  store i8 -10, ptr %4, align 1
  %5 = getelementptr [16 x i8], ptr %key, i32 0, i32 4
  store i8 -32, ptr %5, align 1
  %6 = getelementptr [16 x i8], ptr %key, i32 0, i32 5
  store i8 -111, ptr %6, align 1
  %7 = getelementptr [16 x i8], ptr %key, i32 0, i32 6
  store i8 -40, ptr %7, align 1
  %8 = getelementptr [16 x i8], ptr %key, i32 0, i32 7
  store i8 -108, ptr %8, align 1
  %9 = getelementptr [16 x i8], ptr %key, i32 0, i32 8
  store i8 -53, ptr %9, align 1
  %10 = getelementptr [16 x i8], ptr %key, i32 0, i32 9
  store i8 -31, ptr %10, align 1
  %11 = getelementptr [16 x i8], ptr %key, i32 0, i32 10
  store i8 -91, ptr %11, align 1
  %12 = getelementptr [16 x i8], ptr %key, i32 0, i32 11
  store i8 -7, ptr %12, align 1
  %13 = getelementptr [16 x i8], ptr %key, i32 0, i32 12
  store i8 -75, ptr %13, align 1
  %14 = getelementptr [16 x i8], ptr %key, i32 0, i32 13
  store i8 -76, ptr %14, align 1
  %15 = getelementptr [16 x i8], ptr %key, i32 0, i32 14
  store i8 -45, ptr %15, align 1
  %16 = getelementptr [16 x i8], ptr %key, i32 0, i32 15
  store i8 -107, ptr %16, align 1
  %i = alloca i64, align 8
  store i64 0, ptr %i, align 8
  %17 = load ptr, ptr getelementptr (i64, ptr @12, i64 2), align 8
  %18 = getelementptr i8, ptr %17, i32 831366031
  store i32 -1327483469, ptr @25, align 4
  %caseKeyPtr = alloca i32, align 4
  store i32 1805598599, ptr %caseKeyPtr, align 4
  br label %switchLoopEntry

switchLoopEntry:                                  ; preds = %loopbr, %switchLoopEnd, %entry
  %loadGlobalSwitchVar = load i32, ptr @25, align 4
  br label %NodeBlock10

NodeBlock10:                                      ; preds = %switchLoopEntry
  %Pivot11 = icmp slt i32 %loadGlobalSwitchVar, -1327483469
  br i1 %Pivot11, label %LeafBlock, label %NodeBlock

NodeBlock:                                        ; preds = %NodeBlock10
  %Pivot = icmp slt i32 %loadGlobalSwitchVar, -262132012
  br i1 %Pivot, label %LeafBlock6, label %LeafBlock8

LeafBlock8:                                       ; preds = %NodeBlock
  %SwitchLeaf9 = icmp eq i32 %loadGlobalSwitchVar, -262132012
  br i1 %SwitchLeaf9, label %exit, label %switchDefault

LeafBlock6:                                       ; preds = %NodeBlock
  %SwitchLeaf7 = icmp eq i32 %loadGlobalSwitchVar, -1327483469
  br i1 %SwitchLeaf7, label %loop, label %switchDefault

LeafBlock:                                        ; preds = %NodeBlock10
  %SwitchLeaf = icmp eq i32 %loadGlobalSwitchVar, -1738117861
  br i1 %SwitchLeaf, label %loopbr, label %switchDefault

switchDefault:                                    ; preds = %LeafBlock8, %LeafBlock6, %LeafBlock
  br label %switchLoopEnd

loop:                                             ; preds = %LeafBlock6
  %iLoad = load i64, ptr %i, align 8
  store i64 %iLoad, ptr %iLoad.reg2mem, align 8
  %iLoad.reload5 = load i64, ptr %iLoad.reg2mem, align 8
  %cond = icmp slt i64 %iLoad.reload5, 15
  %19 = select i1 %cond, i32 -1667879518, i32 -721919326
  %20 = select i1 %cond, i32 0, i32 1
  %21 = getelementptr i64, ptr @12, i32 %20
  %22 = load ptr, ptr %21, align 8
  %23 = sub i32 0, %19
  %24 = getelementptr i8, ptr %22, i32 %23
  indirectbr ptr %24, [label %loopbr, label %exit]

loopbr:                                           ; preds = %LeafBlock, %loop
  %iLoad.reload4 = load i64, ptr %iLoad.reg2mem, align 8
  %strPtr = getelementptr i8, ptr %strArg, i64 %iLoad.reload4
  %strLoad = load i8, ptr %strPtr, align 1
  %iLoad.reload3 = load i64, ptr %iLoad.reg2mem, align 8
  %25 = urem i64 %iLoad.reload3, 16
  %keyPtr = getelementptr i8, ptr %key, i64 %25
  %keyLoad = load i8, ptr %keyPtr, align 1
  %xorValue = xor i8 %strLoad, %keyLoad
  %.reload1 = load ptr, ptr %.reg2mem, align 8
  %iLoad.reload2 = load i64, ptr %iLoad.reg2mem, align 8
  %outPtr = getelementptr i8, ptr %.reload1, i64 %iLoad.reload2
  store i8 %xorValue, ptr %outPtr, align 1
  %iLoad.reload = load i64, ptr %iLoad.reg2mem, align 8
  %iNext = add i64 %iLoad.reload, 1
  store i64 %iNext, ptr %i, align 8
  %26 = load ptr, ptr getelementptr (i64, ptr @12, i64 2), align 8
  %27 = getelementptr i8, ptr %26, i32 831366031
  %caseKeyTmp = load i32, ptr %caseKeyPtr, align 4
  %28 = sub i32 %caseKeyTmp, -1161885228
  store i32 %28, ptr @25, align 4
  br label %switchLoopEntry

exit:                                             ; preds = %LeafBlock8, %loop
  %.reload = load ptr, ptr %.reg2mem, align 8
  ret ptr %.reload

switchLoopEnd:                                    ; preds = %switchDefault
  br label %switchLoopEntry
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

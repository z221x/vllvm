// VMP 顶层 ABI：R0-R5 传递六个标量参数，R0 同时承载返回值。
def RetCC_BPF64 : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[R0]>>
]>;

def CC_BPF64 : CallingConv<[
  CCIfType<[i8, i16, i32], CCPromoteToType<i64>>,
  CCIfType<[i64], CCAssignToReg<[R0, R1, R2, R3, R4, R5]>>,
  CCAssignToStack<8, 8>
]>;

def RetCC_BPF32 : CallingConv<[
  CCIfType<[i32], CCAssignToRegWithShadow<[W0], [R0]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[R0], [W0]>>
]>;

def CC_BPF32 : CallingConv<[
  CCIfType<[i32], CCAssignToRegWithShadow<[W0, W1, W2, W3, W4, W5],
                                          [R0, R1, R2, R3, R4, R5]>>,
  CCIfType<[i64], CCAssignToRegWithShadow<[R0, R1, R2, R3, R4, R5],
                                          [W0, W1, W2, W3, W4, W5]>>,
  CCAssignToStack<8, 8>
]>;

def CSR : CalleeSavedRegs<(add R6, R7, R8, R9, R10)>;
def CSR_PreserveAll : CalleeSavedRegs<
    (add R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10)>;

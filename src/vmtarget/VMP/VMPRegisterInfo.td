// VMP 寄存器编码与公共 ISA 的 4 位字段一一对应。R12/R13 在 M3 翻译器
// 中暂作为分支展开临时寄存器，因此后端会把它们标为 reserved；寄存器类仍
// 冻结为完整的 R0-R13，后续原生 VMP ISel 可直接启用这两个寄存器。
let Namespace = "BPF" in {
  def sub_32 : SubRegIndex<32>;
}

class Wi<bits<16> Enc, string Name> : Register<Name> {
  let HWEncoding = Enc;
  let Namespace = "BPF";
}

class Ri<bits<16> Enc, string Name, list<Register> SubRegs>
    : RegisterWithSubRegs<Name, SubRegs> {
  let HWEncoding = Enc;
  let Namespace = "BPF";
  let SubRegIndices = [sub_32];
}

foreach I = 0-13 in {
  def W#I : Wi<I, "w"#I>, DwarfRegNum<[I]>;
  def R#I : Ri<I, "r"#I, [!cast<Wi>("W"#I)]>, DwarfRegNum<[I]>;
}

def SA : Register<"sa"> {
  let HWEncoding = 14;
  let Namespace = "BPF";
}
def ZR : Register<"zr"> {
  let HWEncoding = 15;
  let Namespace = "BPF";
}

def GPR32 : RegisterClass<"BPF", [i32], 64, (add
  (sequence "W%u", 1, 9), W12, W13, W0, W11, W10
)>;

def GPR : RegisterClass<"BPF", [i64], 64, (add
  (sequence "R%u", 1, 9), R12, R13, R0, R11, R10
)>;

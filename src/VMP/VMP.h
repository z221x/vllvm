#pragma once

namespace llvm {

class Pass;
class raw_pwrite_stream;

Pass *createVMPCodeEmitterPass(raw_pwrite_stream &Output);

} // namespace llvm

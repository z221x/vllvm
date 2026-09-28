#!/usr/bin/env python3
"""把 bitcode 转成可编译进 LLVMVLLVM 的只读字节数组。

用法：embed_bitcode.py INPUT.bc OUTPUT.inc [SYMBOL]
SYMBOL 默认 VmpRuntimeBitcode（解释器运行时沿用旧名）。
"""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print("usage: embed_bitcode.py INPUT.bc OUTPUT.inc [SYMBOL]",
              file=sys.stderr)
        return 2
    symbol = sys.argv[3] if len(sys.argv) == 4 else "VmpRuntimeBitcode"
    data = Path(sys.argv[1]).read_bytes()
    lines = [f"static constexpr unsigned char {symbol}[] = {{"]
    for start in range(0, len(data), 16):
        chunk = data[start : start + 16]
        lines.append("  " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    lines.append("};")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

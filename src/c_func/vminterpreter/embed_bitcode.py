#!/usr/bin/env python3
"""把解释器 bitcode 转成可编译进 LLVMVLLVM 的只读字节数组。"""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: embed_bitcode.py INPUT.bc OUTPUT.inc", file=sys.stderr)
        return 2
    data = Path(sys.argv[1]).read_bytes()
    lines = ["static constexpr unsigned char VmpRuntimeBitcode[] = {"]
    for start in range(0, len(data), 16):
        chunk = data[start : start + 16]
        lines.append("  " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    lines.append("};")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""向 clang -S -emit-llvm 产出的 LLVM IR 文本注入 vllvm 函数属性。

等价于在源码上写 __attribute__((annotate("vllvm:<kind>")))，但能保证
模块内所有函数（或 --only 指定的函数）被一致标注，避免对开源项目
源码做脆弱的正则改写。只处理 define 行，declare 保持不变。
"""
import argparse
import re
import sys

DEFINE_RE = re.compile(r"(?m)^define\b")
NAME_RE = re.compile(r"@(?:(\"(?:[^\"\\]|\\.)*\")|([-A-Za-z$._][-A-Za-z$._0-9]*))")


def find_header_end(text: str, start: int) -> int:
    """返回 define 头部结束的 '{' 下标，跳过引号内的 '{'。"""
    i = start
    in_string = False
    while i < len(text):
        c = text[i]
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c == ";":  # define 头部不会有注释，防御性处理
            nl = text.find("\n", i)
            if nl == -1:
                break
            i = nl
        elif c == "{":
            return i
        i += 1
    return -1


def inject_attrs(text: str, attrs, only=None, exclude=None):
    pieces = []
    last = 0
    count = 0
    for m in DEFINE_RE.finditer(text):
        brace = find_header_end(text, m.start())
        if brace == -1:
            continue
        header = text[m.start():brace]
        nm = None
        for g in NAME_RE.finditer(header[len("define"):]):
            nm = g.group(1) or g.group(2)
            break
        if nm is None:
            continue
        if only is not None and nm not in only:
            continue
        if exclude is not None and nm in exclude:
            continue
        attr_str = " ".join('"%s"' % a for a in attrs)
        pieces.append(text[last:brace])
        if text[brace - 1] not in " \t":
            pieces.append(" ")
        pieces.append(attr_str + " ")
        last = brace
        count += 1
    pieces.append(text[last:])
    return "".join(pieces), count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--attrs", required=True, help='逗号分隔，如 vllvm.fla,vllvm.bcf')
    ap.add_argument("--only", help="只标注这些函数（逗号分隔的 IR 函数名）")
    ap.add_argument("--exclude", help="排除这些函数")
    args = ap.parse_args()

    attrs = [a.strip() for a in args.attrs.split(",") if a.strip()]
    if not attrs or not all(a.startswith("vllvm.") for a in attrs):
        sys.exit("attrs 必须形如 vllvm.<kind>")

    with open(args.input) as f:
        text = f.read()
    out, count = inject_attrs(
        text, attrs,
        only=set(x for x in args.only.split(",")) if args.only else None,
        exclude=set(x for x in args.exclude.split(",")) if args.exclude else None,
    )
    with open(args.output, "w") as f:
        f.write(out)
    print("%s: %d functions annotated" % (args.output, count))


if __name__ == "__main__":
    main()

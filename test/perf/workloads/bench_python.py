# CPython 确定性负载：数值循环 + 字符串/表操作 + 简单 JSON 风格序列化，
# 输出校验和供跨配置比对。
import json

acc = 0
for i in range(120000):
    acc = (acc * 1103515245 + 12345) & 0xFFFFFFFF
    acc ^= acc >> 7

s = 0
parts = []
for i in range(2000):
    parts.append("seg%04d" % (i * 7 % 9973))
    s += len(parts[-1])
joined = "-".join(parts)

table = {}
for i in range(4000):
    table["k%05d" % (i * 13 % 99991)] = (i * 31) & 0xFFFF
tsum = sum(table.values())

blob = json.dumps({"parts": parts[:200], "table": table},
                  sort_keys=True).encode()
h = 2166136261
for b in blob:
    h = ((h ^ b) * 16777619) & 0xFFFFFFFF

print("%08x %d %d %d" % (h, s, tsum, len(joined)))

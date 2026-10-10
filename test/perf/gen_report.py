#!/usr/bin/env python3
"""把 run_perfbench.py 的 results.json 汇总为 Markdown 性能报告。"""
import argparse
import json
import os
import statistics
from collections import defaultdict

CONFIG_ORDER = ["base", "fla", "bcf", "ibr", "icall", "vmfla", "enstr",
                "bb2func", "merge", "vmp", "combo", "combo2"]
CONFIG_DESC = {
    "base": "基线（无混淆）",
    "fla": "控制流平坦化",
    "bcf": "虚假控制流",
    "ibr": "间接分支",
    "icall": "间接调用（含参数加密）",
    "vmfla": "VM 平坦化（bb2func+merge+vmfla 链，含 lvars 结构化）",
    "enstr": "字符串加密",
    "bb2func": "CFG 区域提取为 helper",
    "merge": "函数融合进 keyed dispatcher",
    "vmp": "虚拟机保护（实验性）",
    "combo": "组合（fla+bcf+ibr+icall+vmfla）",
    "combo2": "组合（fla+bcf+ibr+vmfla，无 icall）",
}
APP_DESC = {
    "bzip2": "bzip2 1.0.8 压缩/解压",
    "zlib": "zlib 1.3.1 压缩/解压",
    "lua": "lua 5.4.7 n-body+字符串",
    "sqlite": "sqlite 3.45.1 内存库",
}


def fmt_ratio(r):
    if r is None:
        return "-"
    return "%.2fx" % r


def app_table(results, app):
    lines = ["| 配置 | 运行中位 (s) | vs base | strip 后大小 | 体积比 | __TEXT | 编译耗时 (s) | 状态 |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    base = results.get(app, {}).get("base", {})
    base_t = base.get("median_s")
    base_s = base.get("size_bytes")
    for cfg in CONFIG_ORDER:
        r = results.get(app, {}).get(cfg)
        if not r:
            continue
        if r["status"] != "ok":
            err = (r.get("error") or r["status"])[:60]
            lines.append("| %s | - | - | - | - | - | - | ❌ %s |" % (cfg, err))
            continue
        rt = fmt_ratio(r["median_s"] / base_t if base_t else None)
        rs = fmt_ratio(r["size_bytes"] / base_s if base_s else None)
        text = r.get("text_bytes")
        lines.append("| %s | %.3f | %s | %d | %s | %s | %s | ✅ |"
                     % (cfg, r["median_s"], rt, r["size_bytes"], rs,
                        ("%d" % text) if text else "-", r.get("build_s")))
    return lines


def geo(vals):
    vals = [v for v in vals if v]
    if not vals:
        return None
    return statistics.geometric_mean(vals)


def summary_table(results):
    lines = ["| Pass | " + " | ".join(results.keys()) + " | 几何平均 |",
             "| --- | " + " | ".join(["---"] * len(results)) + " | --- |"]
    for cfg in CONFIG_ORDER:
        if cfg == "base":
            continue
        row = []
        ratios = []
        for app, cfgs in results.items():
            base = cfgs.get("base", {})
            r = cfgs.get(cfg, {})
            if r.get("status") == "ok" and base.get("median_s"):
                ratio = r["median_s"] / base["median_s"]
                ratios.append(ratio)
                row.append("%.2fx" % ratio)
            else:
                row.append("❌")
        g = geo(ratios)
        lines.append("| %s | %s | %s |"
                     % (cfg, " | ".join(row), ("%.2fx" % g) if g else "-"))
    return lines


def size_summary(results):
    lines = ["| Pass | " + " | ".join(results.keys()) + " | 几何平均 |",
             "| --- | " + " | ".join(["---"] * len(results)) + " | --- |"]
    for cfg in CONFIG_ORDER:
        if cfg == "base":
            continue
        row = []
        ratios = []
        for app, cfgs in results.items():
            base = cfgs.get("base", {})
            r = cfgs.get(cfg, {})
            if r.get("status") == "ok" and base.get("size_bytes"):
                ratio = r["size_bytes"] / base["size_bytes"]
                ratios.append(ratio)
                row.append("%.2fx" % ratio)
            else:
                row.append("❌")
        g = geo(ratios)
        lines.append("| %s | %s | %s |"
                     % (cfg, " | ".join(row), ("%.2fx" % g) if g else "-"))
    return lines


def build_summary(results):
    lines = ["| Pass | " + " | ".join(results.keys()) + " |",
             "| --- | " + " | ".join(["---"] * len(results)) + " |"]
    for cfg in CONFIG_ORDER:
        row = []
        for app, cfgs in results.items():
            base = cfgs.get("base", {})
            r = cfgs.get(cfg, {})
            if r.get("status") == "ok" and base.get("build_s") is not None:
                b = r.get("build_s")
                if b is not None:
                    row.append("%.1fs (%.1fx)" % (b, b / max(base["build_s"], 0.001)))
                else:
                    row.append("-")
            else:
                row.append("❌")
        lines.append("| %s | %s |" % (cfg, " | ".join(row)))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results_json")
    ap.add_argument("--output", required=True)
    ap.add_argument("--machine", default="")
    ap.add_argument("--toolchain", default="")
    ap.add_argument("--date", default="")
    args = ap.parse_args()

    with open(args.results_json) as f:
        results = json.load(f)

    out = []
    out.append("# VLLVM Pass 开源应用性能测试报告\n")
    out.append("- 日期：%s" % (args.date or "-"))
    out.append("- 测试机：%s" % (args.machine or "-"))
    out.append("- 工具链：%s" % (args.toolchain or "-"))
    out.append("")
    out.append("## 测试目的\n")
    out.append("在多个开源 C 项目上以“全程序函数标注”方式逐一启用 VLLVM 各混淆 Pass，"
               "测量相对无混淆基线的**运行时开销、二进制体积开销、编译耗时开销**，"
               "并校验混淆产物功能正确性（输出与基线逐字节一致）。\n")

    out.append("## 被测应用\n")
    out.append("| 应用 | 说明 | 负载 |")
    out.append("| --- | --- | --- |")
    for app in results:
        out.append("| %s | %s | 见下文各表 |" % (app, APP_DESC.get(app, app)))
    out.append("")

    out.append("## 运行时开销\n")
    for app in results:
        out.append("### %s\n" % app)
        out.extend(app_table(results, app))
        out.append("")

    out.append("### 汇总（运行时，vs base）\n")
    out.extend(summary_table(results))
    out.append("")

    out.append("## 二进制体积（strip 后）\n")
    out.extend(size_summary(results))
    out.append("")

    out.append("## 编译耗时（stage2 -O2 全部 TU 墙钟，6 并行）\n")
    out.extend(build_summary(results))
    out.append("")

    with open(args.output, "w") as f:
        f.write("\n".join(out) + "\n")
    print("written:", args.output)


if __name__ == "__main__":
    main()

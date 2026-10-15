# -*- coding: utf-8 -*-
import os, sys
import pandas as pd
sys.stdout.reconfigure(encoding='utf-8')
ROOT = r"c:/Users/15861/WPSDrive/1876856493/WPS企业云盘/中国科学院大学/我的企业文档/大三上学习/操作系统（研讨课）"
x = os.path.join(ROOT, "分组名单——P0P1.xlsx")
dstdir = os.path.join(ROOT, "中间产物", "分组名单"); os.makedirs(dstdir, exist_ok=True)

def esc(v):
    return str(v).replace("|", "\|").replace("\n", " ").strip()

sheets = pd.read_excel(x, sheet_name=None, header=None)
lines = ["# 分组名单——P0P1\n", f"> 源文件：`分组名单——P0P1.xlsx`　sheet 数：{len(sheets)}\n"]
for name, df in sheets.items():
    lines.append(f"\n## Sheet: {name}　({df.shape[0]} 行 × {df.shape[1]} 列)\n")
    for _, row in df.iterrows():
        cells = [esc(c) for c in row.tolist()]
        if not any(cells):
            continue
        lines.append("\t".join(cells).rstrip())
with open(os.path.join(dstdir, "分组名单——P0P1.md"), "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print("\n".join(lines[:60]))

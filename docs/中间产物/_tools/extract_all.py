# -*- coding: utf-8 -*-
"""把课程目录下所有 PDF / xlsx 抽取为 Markdown 中间产物。"""
import os, sys, re
import pymupdf
import pandas as pd

sys.stdout.reconfigure(encoding='utf-8')
ROOT = r"c:/Users/15861/WPSDrive/1876856493/WPS企业云盘/中国科学院大学/我的企业文档/大三上学习/操作系统（研讨课）"
OUT  = os.path.join(ROOT, "中间产物")

JOBS = [
    ("操作系统研讨课-Lecture 0.pdf",         "Lecture0-课程导论"),
    ("操作系统研讨课-Lecture 1.pdf",         "Lecture1-Bootloader"),
    ("操作系统研讨课-Lecture 2.pdf",         "Lecture2-简易内核"),
    ("操作系统研讨课-Project 0.pdf",         "Project0-课程环境与预备知识"),
    ("guide_book_p1.pdf",                    "GuideBook-P1-引导与镜像"),
    ("guide_book_p2.pdf",                    "GuideBook-P2-简易内核"),
    ("P2start-code和pynq板卡启动.pdf",        "P2-startcode与板卡启动"),
]

def clean(t: str) -> str:
    t = t.replace('\u00ad', '')
    t = re.sub(r'[ \t]+\n', '\n', t)
    t = re.sub(r'\n{3,}', '\n\n', t)
    return t.strip()

for src, d in JOBS:
    srcp = os.path.join(ROOT, src)
    if not os.path.exists(srcp):
        print("[MISS]", src); continue
    dstdir = os.path.join(OUT, d)
    os.makedirs(dstdir, exist_ok=True)
    doc = pymupdf.open(srcp)
    parts = [f"# {os.path.splitext(src)[0]}\n",
             f"> 源文件：`{src}`　页数：{doc.page_count}",
             ""]
    for i, page in enumerate(doc):
        txt = clean(page.get_text())
        nimg = len(page.get_images(full=True))
        parts.append(f"\n---\n\n## Page {i+1}" + (f"　*({nimg} 张图)*" if nimg else ""))
        parts.append("")
        parts.append(txt if txt else "*（本页无可抽取文本，内容以图片/公式为主）*")
    md = "\n".join(parts) + "\n"
    outmd = os.path.join(dstdir, os.path.splitext(src)[0] + ".md")
    with open(outmd, "w", encoding="utf-8") as f:
        f.write(md)
    print(f"[OK] {src:42s} -> {d}/{os.path.basename(outmd)}  pages={doc.page_count}  chars={len(md)}")
    doc.close()

x = os.path.join(ROOT, "分组名单——P0P1.xlsx")
if os.path.exists(x):
    dstdir = os.path.join(OUT, "分组名单"); os.makedirs(dstdir, exist_ok=True)
    sheets = pd.read_excel(x, sheet_name=None, header=None)
    lines = ["# 分组名单——P0P1\n", f"> 源文件：`分组名单——P0P1.xlsx`　sheet 数：{len(sheets)}\n"]
    for name, df in sheets.items():
        lines.append(f"\n## Sheet: {name}　({df.shape[0]} 行 × {df.shape[1]} 列)\n")
        lines.append(df.fillna("").to_markdown(index=False, headers=[]))
    with open(os.path.join(dstdir, "分组名单——P0P1.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("[OK] 分组名单——P0P1.xlsx -> 分组名单/分组名单——P0P1.md")

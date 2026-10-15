# -*- coding: utf-8 -*-
"""P3 教材归档：guide_book_p3.pdf + Lecture 3.pdf → 中间产物/P3-*/（md 全文 + 关键页 png）。

用法："D:/anaconda3/envs/pytorch_env/python.exe" 中间产物/_tools/extract_p3.py
路径从本脚本位置推出（源 PDF 走作业根下的 junction「操作系统（研讨课）」），不依赖云盘绝对路径。
"""
import os
import re
import sys

import pymupdf

sys.stdout.reconfigure(encoding='utf-8')

HERE = os.path.dirname(os.path.abspath(__file__))          # 中间产物/_tools
OUT_ROOT = os.path.dirname(HERE)                            # 中间产物
SRC_ROOT = os.path.join(os.path.dirname(OUT_ROOT), "操作系统（研讨课）")
THEME = "P3-进程管理、通信与多核执行"

# (源 PDF, 主题内子文件夹, 关键页列表)；关键页 = 有图/截图的页（2026-10-14 探测：位置非页脚贴片）
JOBS = [
    ("guide_book_p3.pdf", "GuideBook-P3-进程管理、通信与多核执行", [5, 8, 10, 12, 16, 17]),
    ("操作系统研讨课-Lecture 3.pdf", "Lecture3-进程管理、通信与多核执行", [8, 12, 15, 20]),
]


def clean(t: str) -> str:
    t = t.replace('\u00ad', '')
    t = re.sub(r'[ \t]+\n', '\n', t)
    t = re.sub(r'\n{3,}', '\n\n', t)
    return t.strip()


for src, d, pages in JOBS:
    srcp = os.path.join(SRC_ROOT, src)
    dstdir = os.path.join(OUT_ROOT, THEME, d)
    os.makedirs(os.path.join(dstdir, "pages"), exist_ok=True)
    doc = pymupdf.open(srcp)

    parts = [f"# {os.path.splitext(src)[0]}\n",
             f"> 源文件：`{src}`　页数：{doc.page_count}",
             ""]
    for i, page in enumerate(doc):
        txt = clean(page.get_text())
        nimg = len(page.get_images(full=True))
        parts.append(f"\n---\n\n## Page {i + 1}" + (f"　*({nimg} 张图)*" if nimg else ""))
        parts.append("")
        parts.append(txt if txt else "*（本页无可抽取文本，内容以图片/公式为主）*")
    outmd = os.path.join(dstdir, os.path.splitext(src)[0] + ".md")
    with open(outmd, "w", encoding="utf-8") as f:
        f.write("\n".join(parts) + "\n")
    print(f"[OK] {outmd}  pages={doc.page_count}  chars={sum(len(p) for p in parts)}")

    for p in pages:
        if p > doc.page_count:
            continue
        pix = doc[p - 1].get_pixmap(dpi=130)
        f = os.path.join(dstdir, "pages", f"p{p:02d}.png")
        pix.save(f)
        print(f"[OK] {THEME}/{d}/pages/p{p:02d}.png  {pix.width}x{pix.height}  {os.path.getsize(f) // 1024}KB")
    doc.close()

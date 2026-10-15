# -*- coding: utf-8 -*-
import os, sys, pymupdf
sys.stdout.reconfigure(encoding='utf-8')
ROOT = r"c:/Users/15861/WPSDrive/1876856493/WPS企业云盘/中国科学院大学/我的企业文档/大三上学习/操作系统（研讨课）"
OUT = os.path.join(ROOT, "中间产物")
JOBS = [("操作系统研讨课-Lecture 2.pdf", "P2-简易内核/Lecture2-简易内核", [48]),
        ("操作系统研讨课-Lecture 0.pdf", "00-课程总览/Lecture0-课程导论", [8, 9])]
for src, d, pages in JOBS:
    doc = pymupdf.open(os.path.join(ROOT, src))
    dstdir = os.path.join(OUT, d, "pages"); os.makedirs(dstdir, exist_ok=True)
    for p in pages:
        pix = doc[p-1].get_pixmap(dpi=130)
        f = os.path.join(dstdir, f"p{p:02d}.png"); pix.save(f)
        print(f"[OK] {d}/pages/p{p:02d}.png")
    doc.close()

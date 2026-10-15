# -*- coding: utf-8 -*-
"""把关键图页渲染为 PNG，存到各自的中间产物文件夹下 pages/。"""
import os, sys
import pymupdf
sys.stdout.reconfigure(encoding='utf-8')
ROOT = r"c:/Users/15861/WPSDrive/1876856493/WPS企业云盘/中国科学院大学/我的企业文档/大三上学习/操作系统（研讨课）"
OUT  = os.path.join(ROOT, "中间产物")

JOBS = [
    ("操作系统研讨课-Lecture 2.pdf", "Lecture2-简易内核",
     [3,7,9,11,13,15,19,20,27,29,31,33,37,38,44,45,47]),
    ("操作系统研讨课-Lecture 1.pdf", "Lecture1-Bootloader",
     [3,6,18,19]),
    ("P2start-code和pynq板卡启动.pdf", "P2-startcode与板卡启动", [2,3]),
    ("guide_book_p2.pdf", "GuideBook-P2-简易内核", [6,9,12,20,24,25]),
]

for src, d, pages in JOBS:
    srcp = os.path.join(ROOT, src)
    dstdir = os.path.join(OUT, d, "pages"); os.makedirs(dstdir, exist_ok=True)
    doc = pymupdf.open(srcp)
    for p in pages:
        if p > doc.page_count: continue
        pix = doc[p-1].get_pixmap(dpi=130)
        f = os.path.join(dstdir, f"p{p:02d}.png")
        pix.save(f)
        print(f"[OK] {d}/pages/p{p:02d}.png  {pix.width}x{pix.height}  {os.path.getsize(f)//1024}KB")
    doc.close()

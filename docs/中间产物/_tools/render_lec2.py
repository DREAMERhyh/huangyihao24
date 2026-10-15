# -*- coding: utf-8 -*-
import os, sys, pymupdf
sys.stdout.reconfigure(encoding='utf-8')
ROOT = r"c:/Users/15861/WPSDrive/1876856493/WPS企业云盘/中国科学院大学/我的企业文档/大三上学习/操作系统（研讨课）"
d = os.path.join(ROOT, "中间产物", "P2-简易内核", "Lecture2-简易内核", "pages")
os.makedirs(d, exist_ok=True)
doc = pymupdf.open(os.path.join(ROOT, "操作系统研讨课-Lecture 2.pdf"))
for p in [3, 27, 29, 32, 33, 38, 44, 46]:
    pix = doc[p-1].get_pixmap(dpi=130)
    pix.save(os.path.join(d, f"p{p:02d}.png"))
    print(f"[OK] p{p:02d}.png")
doc.close()

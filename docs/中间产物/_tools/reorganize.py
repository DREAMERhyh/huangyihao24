# -*- coding: utf-8 -*-
"""把中间产物按 project 重组目录。"""
import os, shutil, sys
sys.stdout.reconfigure(encoding='utf-8')
OUT = os.path.dirname(os.path.abspath(__file__)).replace("\_tools", "").replace("/_tools", "")

MOVES = {
    "00-课程总览": ["Lecture0-课程导论", "分组名单"],
    "P0-环境与预备知识": ["Project0-课程环境与预备知识"],
    "P1-引导与镜像文件": ["GuideBook-P1-引导与镜像", "Lecture1-Bootloader"],
    "P2-简易内核": ["GuideBook-P2-简易内核", "Lecture2-简易内核", "P2-startcode与板卡启动"],
}

for dst, srcs in MOVES.items():
    os.makedirs(os.path.join(OUT, dst), exist_ok=True)
    for s in srcs:
        sp, dp = os.path.join(OUT, s), os.path.join(OUT, dst, s)
        if os.path.isdir(sp):
            if os.path.exists(dp): shutil.rmtree(dp)
            shutil.move(sp, dp)
            print(f"[MOVE] {s}  ->  {dst}/")

print("\n--- 重组后结构 ---")
for root, dirs, files in os.walk(OUT):
    dirs.sort(); files.sort()
    lvl = root.replace(OUT, "").count(os.sep)
    print("  " * lvl + os.path.basename(root) + "/")
    for f in files:
        print("  " * (lvl + 1) + f)

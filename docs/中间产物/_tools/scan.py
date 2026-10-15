import fitz, os, glob, sys, io
sys.stdout.reconfigure(encoding='utf-8')
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(root))
for f in sorted(glob.glob("*.pdf")):
    doc = fitz.open(f)
    print("="*80)
    print(f"FILE: {f}  pages={doc.page_count}")
    print("META:", {k:v for k,v in doc.metadata.items() if v and k in ('title','author','subject','creator')})
    for i in range(min(2, doc.page_count)):
        t = doc[i].get_text().strip()
        print(f"--- p{i+1} ({len(t)} chars) ---")
        print(t[:900])
    # count images
    imgs = sum(len(doc[i].get_images(full=True)) for i in range(doc.page_count))
    print(f"[images total: {imgs}]")
    doc.close()

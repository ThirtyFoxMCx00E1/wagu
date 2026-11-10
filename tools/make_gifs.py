#!/usr/bin/env python3
"""4.04: chat GIFs. Turns every animated GIF of github.com/ThirtyFoxMCx00E1/animated-gifs into a small
sprite sheet the game can show in the chat (JPEG, up to 12 frames of max 120x90 in 4 columns = one 512x512 texture) plus
index.txt:  id|category|name|frames|cols|frameW|frameH|delayMs
Output: <out>/g/<id>.jpg + <out>/index.txt (pushed to the wagu "gifs" branch; the game downloads one
sheet when it is shown, from mpsamp-open.github.io/gifs/... (the website publishes the gifs branch)).
One GIF at a time is downloaded, converted and dropped (the repo is ~5.8 GB).
  python3 tools/make_gifs.py /tmp/gif_tree.tsv <out>     (tree: "size<TAB>path" from the GitHub tree API)"""
import io, os, sys, urllib.parse, urllib.request, concurrent.futures as cf
from PIL import Image, ImageSequence

RAW = "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/animated-gifs/master/"
MAXF, COLS, FW, FH = 12, 4, 120, 90

def convert(data):
    im = Image.open(io.BytesIO(data))
    frames, durs = [], []
    for fr in ImageSequence.Iterator(im):
        frames.append(fr.convert("RGBA")); durs.append(fr.info.get("duration", 100) or 100)
        if len(frames) >= 400: break
    n = len(frames)
    if n == 0: return None
    pick = [round(i * (n - 1) / (MAXF - 1)) for i in range(MAXF)] if n > MAXF else list(range(n))
    pick = sorted(set(pick))
    total = sum(durs[:n]); delay = max(40, min(1000, int(total / len(pick))))
    w, h = frames[0].size
    k = min(FW / w, FH / h, 1.0); fw, fh = max(8, int(w * k) // 2 * 2), max(8, int(h * k) // 2 * 2)
    cols = min(COLS, len(pick)); rows = (len(pick) + cols - 1) // cols
    sheet = Image.new("RGB", (fw * cols, fh * rows), (20, 22, 26))
    for j, i in enumerate(pick):
        f = frames[i].resize((fw, fh), Image.LANCZOS)
        bg = Image.new("RGBA", f.size, (20, 22, 26, 255)); bg.alpha_composite(f)
        sheet.paste(bg.convert("RGB"), ((j % cols) * fw, (j // cols) * fh))
    out = io.BytesIO(); sheet.save(out, "JPEG", quality=72, optimize=True)
    return out.getvalue(), len(pick), cols, fw, fh, delay

def work(job):
    gid, path, dst = job
    fn = os.path.join(dst, "g", f"{gid}.jpg")
    meta = fn + ".meta"
    if os.path.exists(meta): return open(meta).read().strip()
    url = RAW + urllib.parse.quote(path)
    for attempt in range(3):
        try:
            data = urllib.request.urlopen(url, timeout=90).read(); break
        except Exception as e:
            err = e
    else:
        print("fetch failed", path, err, flush=True); return None
    try: r = convert(data)
    except Exception as e: print("bad gif", path, e, flush=True); return None
    if not r: return None
    jpg, n, cols, fw, fh, delay = r
    open(fn, "wb").write(jpg)
    cat, name = (path.split("/", 1) + [""])[:2] if "/" in path else ("misc", path)
    name = os.path.splitext(os.path.basename(path))[0].replace("|", " ")
    line = f"{gid}|{cat.replace('|', ' ')}|{name}|{n}|{cols}|{fw}|{fh}|{delay}"
    open(meta, "w").write(line)
    return line

def main():
    tree, dst = sys.argv[1], sys.argv[2]
    os.makedirs(os.path.join(dst, "g"), exist_ok=True)
    paths = sorted(p for s, p in (l.rstrip("\n").split("\t", 1) for l in open(tree)) if p.lower().endswith(".gif"))
    jobs = [(i + 1, p, dst) for i, p in enumerate(paths)]
    lines = []
    with cf.ThreadPoolExecutor(12) as ex:
        for k, line in enumerate(ex.map(work, jobs)):
            if line: lines.append(line)
            if k % 100 == 0: print(k, len(jobs), flush=True)
    lines.sort(key=lambda l: int(l.split("|", 1)[0]))
    open(os.path.join(dst, "index.txt"), "w").write("\n".join(lines) + "\n")
    print("done", len(lines), "of", len(jobs))

if __name__ == "__main__":
    main()

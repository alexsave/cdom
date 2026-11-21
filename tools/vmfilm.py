#!/usr/bin/env python3
"""vmfilm.py capture <dir> | build <dir> <out.mp4> [scale]: films the XP guest from the Mac side with no screen recorder: QEMU's
screendump (HMP, through /Users/alex/VMs/ie6-xp/vm.py) about 6 a second, a frame equal to the last dropped, each frame named by its
wall-clock millisecond, and build assembles them with ffmpeg at the real pace (each frame shown for the time until the next:
no speed-up), scaled up <scale> times (default 2, nearest neighbour).  capture runs until it is killed.  Used for the Netscape 2
replay of docs/frames-design.md section 17; macOS's screencapture could not be used, since the QEMU window is a full-screen
Space of its own that is not on screen."""
import os, subprocess, sys, time
sys.path.insert(0, "/Users/alex/VMs/ie6-xp")
if sys.argv[1] == "capture":
    import vm
    d = os.path.abspath(sys.argv[2]); os.makedirs(d, exist_ok=True)
    last = None
    while True:
        t = int(time.time() * 1000)
        f = os.path.join(d, f"{t}.ppm")
        try:
            vm.hmp("screendump " + f)
            time.sleep(0.05)
            # a frame equal to the one before is dropped: it lasts until the next one that differs, which is the same film
            b = open(f, "rb").read()
            if b == last: os.remove(f)
            else: last = b
        except Exception:
            # a screendump that failed or is not written yet (another tool's screendump at the same moment, as
            # tools/vmrender.py's): no frame, and the capture goes on
            time.sleep(0.2)
        time.sleep(0.1)
else:
    d, out = os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
    scale = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    fs = sorted(f for f in os.listdir(d) if f.endswith(".ppm") and os.path.getsize(os.path.join(d, f)) > 1000)
    # the last frame is a screendump that may be unfinished: drop it
    fs = fs[:-1]
    ts = [int(f[:-4]) for f in fs]
    with open(os.path.join(d, "list.txt"), "w") as l:
        for i, f in enumerate(fs):
            dur = (ts[i + 1] - ts[i]) / 1000 if i + 1 < len(fs) else 1.0
            l.write(f"file '{os.path.join(d, f)}'\nduration {dur:.3f}\n")
        l.write(f"file '{os.path.join(d, fs[-1])}'\n")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", os.path.join(d, "list.txt"),
                    "-vf", f"scale=iw*{scale}:ih*{scale}:flags=neighbor,fps=10,format=yuv420p", "-c:v", "libx264", "-crf", "18", out], check=True)
    print(f"{len(fs)} frames, {(ts[-1] - ts[0]) / 1000:.0f} s of wall time -> {out}")

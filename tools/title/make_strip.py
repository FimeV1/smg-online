#!/usr/bin/env python3
"""Developer tool: writes the ONLINE strip (our own artwork) as raw RGB5A3
texture data for the launchers to append to the player's own logo texture.

    python tools/title/make_strip.py [output file]     (default: title/online-strip.bin)

The font is Arial Black unless SMG_TITLE_FONT points at another .ttf.
Needs Pillow."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_title as b
rows = b.draw_strip(440, b.STRIP)
data = b.rgb5a3_encode(rows, 440, b.STRIP)
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "title", "online-strip.bin")
open(out, "wb").write(data)
print("wrote", os.path.normpath(out), len(data), "bytes for", b.STRIP, "rows")
if "--preview" in sys.argv:
    from PIL import Image
    img = Image.new("RGBA", (440, b.STRIP), (20, 40, 90, 255))
    top = Image.new("RGBA", (440, b.STRIP)); top.putdata([c for row in rows for c in row]); img.alpha_composite(top)
    img.save(sys.argv[sys.argv.index("--preview") + 1])

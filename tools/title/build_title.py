#!/usr/bin/env python3
"""
Builds the "Super Mario Galaxy ONLINE" title logo from a player's OWN copy of
LayoutData/TitleLogo.arc (extracted from their disc image).

    python build_title.py <original TitleLogo.arc> <output TitleLogo.arc> [--preview out.png]

What it changes (nothing else in the archive is touched):
  * timg/mytitlelogo.tpl is replaced by a taller image: the original logo
    with an "ONLINE" strip added underneath (appended to the archive)
  * blyt/titlelogo.brlyt: the logo pane is made taller by the same amount and
    moved so the original part stays exactly where it was

The output is an uncompressed RARC, which the game loads fine.
"""
import os
import struct
import sys

STRIP = 68          # extra rows under the logo (multiple of 4); the launchers read it from the strip file size
TEXT = "ONLINE"


# ---- containers ---------------------------------------------------------
def yaz0_dec(d):
    if d[:4] != b"Yaz0":
        return bytes(d)
    size = struct.unpack(">I", d[4:8])[0]
    out = bytearray()
    i = 16
    while len(out) < size:
        code = d[i]
        i += 1
        for _ in range(8):
            if len(out) >= size:
                break
            if code & 0x80:
                out.append(d[i])
                i += 1
            else:
                b1, b2 = d[i], d[i + 1]
                i += 2
                n = b1 >> 4
                if n == 0:
                    n = d[i] + 0x12
                    i += 1
                else:
                    n += 2
                p = len(out) - (((b1 & 0xF) << 8) | b2) - 1
                for _ in range(n):
                    out.append(out[p])
                    p += 1
            code <<= 1
    return bytes(out)


def rarc_entries(d):
    """{path: (entry offset, data offset, size)}"""
    assert d[:4] == b"RARC", "not a RARC archive"
    data_off = struct.unpack(">I", d[0x0C:0x10])[0] + 0x20
    _nn, node_off, _ne, ent_off, _ss, str_off = struct.unpack(">IIIIII", d[0x20:0x38])
    node_off += 0x20
    ent_off += 0x20
    str_off += 0x20

    def name(o):
        return d[str_off + o:d.index(b"\0", str_off + o)].decode("ascii", "replace")

    out = {}

    def walk(n, path):
        _t, _no, _h, cnt, first = struct.unpack(">4sIHHI", d[node_off + n * 16:node_off + n * 16 + 16])
        for k in range(first, first + cnt):
            e = ent_off + k * 20
            _fid, _fh, flags, nameoff, off, size = struct.unpack(">HHHHII", d[e:e + 16])
            nm = name(nameoff)
            if flags & 0x0200:
                if nm not in (".", ".."):
                    walk(off, path + nm + "/")
            else:
                out[path + nm] = (e, data_off + off, size)

    walk(0, "")
    return out, data_off


# ---- RGB5A3 texture -------------------------------------------------------
def rgb5a3_decode(t, w, h, off):
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    p = off
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            for k in range(16):
                v = (t[p] << 8) | t[p + 1]
                p += 2
                if v & 0x8000:
                    c = (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
                else:
                    c = (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7)
                x, y = bx + k % 4, by + k // 4
                if x < w and y < h:
                    px[y][x] = c
    return px


def rgb5a3_encode_px(c):
    r, g, b, a = c
    if a >= 240:
        return 0x8000 | ((r * 31 + 127) // 255 << 10) | ((g * 31 + 127) // 255 << 5) | ((b * 31 + 127) // 255)
    return ((a * 7 + 127) // 255 << 12) | ((r * 15 + 127) // 255 << 8) | ((g * 15 + 127) // 255 << 4) | ((b * 15 + 127) // 255)


def rgb5a3_encode(px, w, h):
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            for k in range(16):
                x, y = bx + k % 4, by + k // 4
                v = rgb5a3_encode_px(px[y][x]) if x < w and y < h else 0
                out += struct.pack(">H", v)
    return bytes(out)


# ---- the ONLINE strip -----------------------------------------------------
def draw_strip(w, h):
    """Chunky white letters with a grey 3D edge and dark outline, in the
    spirit of the logo's SUPER / GALAXY lettering. Returns rows of RGBA."""
    from PIL import Image, ImageDraw, ImageFont

    scale = 4  # supersample for clean edges
    img = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)
    # SMG_TITLE_FONT picks the font (a .ttf path); only the rendered image is
    # shipped, never the font file.
    path = None
    for cand in (os.environ.get("SMG_TITLE_FONT", ""), r"C:\Windows\Fonts\ariblk.ttf", r"C:\Windows\Fonts\impact.ttf",
                 "/System/Library/Fonts/Supplemental/Arial Black.ttf", "/Library/Fonts/Arial Black.ttf",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if cand and os.path.exists(cand):
            path = cand
            break
    if path is None:
        raise SystemExit("no bold font found to draw the ONLINE text")

    spacing = int(4 * scale)
    depth = int(3 * scale)
    outline = int(1.8 * scale)
    # Largest size whose letters fit the strip both ways (wide fonts hit the
    # width limit first, tall ones the height limit)
    size = int(h * scale * 1.2)
    while True:
        font = ImageFont.truetype(path, size)
        widths = [dr.textbbox((0, 0), ch, font=font) for ch in TEXT]
        total = sum(b[2] - b[0] for b in widths) + spacing * (len(TEXT) - 1)
        top = min(b[1] for b in widths)
        bottom = max(b[3] for b in widths)
        fits_w = total + 2 * outline + depth <= w * scale * 0.985
        fits_h = (bottom - top) + 2 * outline + depth <= h * scale * 0.93
        if (fits_w and fits_h) or size <= 8:
            break
        size -= 2
    x = (w * scale - total) // 2
    y = outline + int(1 * scale) - top   # hug the logo above; spare rows go to the bottom
    for ch, b in zip(TEXT, widths):
        cx = x - b[0]
        # extruded side (down-right), dark to light
        for k in range(depth, 0, -1):
            shade = 70 + (depth - k) * 90 // depth
            dr.text((cx + k, y + k), ch, font=font, fill=(shade, shade, shade + 10, 255),
                    stroke_width=outline, stroke_fill=(25, 25, 35, 255))
        dr.text((cx, y), ch, font=font, fill=(255, 255, 255, 255), stroke_width=outline, stroke_fill=(25, 25, 35, 255))
        x += (b[2] - b[0]) + spacing
    img = img.resize((w, h), Image.LANCZOS)
    data = list(img.getdata()) if not hasattr(img, "get_flattened_data") else list(img.get_flattened_data())
    return [data[r * w:(r + 1) * w] for r in range(h)]


# ---- main -----------------------------------------------------------------
def build(src, dst, preview=None):
    d = bytearray(yaz0_dec(open(src, "rb").read()))
    entries, data_off = rarc_entries(bytes(d))

    # texture: original logo + strip
    e, o, n = entries["timg/mytitlelogo.tpl"]
    tpl = bytes(d[o:o + n])
    magic, count, tab = struct.unpack(">III", tpl[:12])
    assert magic == 0x0020AF30 and count == 1, "unexpected logo texture"
    ih = struct.unpack(">I", tpl[tab:tab + 4])[0]
    h, w, fmt, off = struct.unpack(">HHII", tpl[ih:ih + 12])
    assert (w, h, fmt) == (440, 256, 5), "unexpected logo texture format %r (already patched?)" % ((w, h, fmt),)
    px = rgb5a3_decode(tpl, w, h, off)
    px += draw_strip(w, STRIP)
    new_h = h + STRIP
    new_tpl = bytearray(tpl[:off])
    struct.pack_into(">H", new_tpl, ih, new_h)
    new_tpl += rgb5a3_encode(px, w, new_h)

    # append it to the archive's data area and repoint the file entry
    while len(d) % 32:
        d.append(0)
    new_off = len(d)
    d += new_tpl
    while len(d) % 32:
        d.append(0)
    struct.pack_into(">II", d, e + 8, new_off - data_off, len(new_tpl))
    grown = len(d) - struct.unpack(">I", d[4:8])[0]
    struct.pack_into(">I", d, 4, len(d))
    for field in (0x10, 0x14):                     # data length, MRAM size
        struct.pack_into(">I", d, field, struct.unpack(">I", d[field:field + 4])[0] + grown)

    # layout: make the logo pane taller, keep the original art where it was
    e, o, n = entries["blyt/titlelogo.brlyt"]
    pos = o + 16
    inside = 0          # nesting depth below the PicLogo pane
    seen_logo = False
    while pos < o + n:
        tag = bytes(d[pos:pos + 4])
        size = struct.unpack(">I", d[pos + 4:pos + 8])[0]
        if tag in (b"pan1", b"pic1"):
            name = bytes(d[pos + 12:pos + 28]).split(b"\0")[0]
            ty = struct.unpack(">f", d[pos + 0x28:pos + 0x2C])[0]
            if name == b"PicLogo":
                ph = struct.unpack(">f", d[pos + 0x48:pos + 0x4C])[0]
                struct.pack_into(">f", d, pos + 0x48, ph + STRIP)
                struct.pack_into(">f", d, pos + 0x28, ty - STRIP / 2.0)
                seen_logo = True
            elif inside:
                # children are placed relative to the pane's centre, which moved down
                struct.pack_into(">f", d, pos + 0x28, ty + STRIP / 2.0)
        elif tag == b"pas1" and seen_logo:
            inside += 1
        elif tag == b"pae1" and inside:
            inside -= 1
            if not inside:
                seen_logo = False
        elif seen_logo and not inside:
            seen_logo = False
        pos += size
    with open(dst, "wb") as f:
        f.write(d)

    if preview:
        from PIL import Image
        img = Image.new("RGBA", (w, new_h))
        img.putdata([c for row in px for c in row])
        bg = Image.new("RGBA", (w, new_h), (20, 40, 90, 255))
        bg.alpha_composite(img)
        bg.save(preview)
    return len(d)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    pv = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    print("wrote", sys.argv[2], build(sys.argv[1], sys.argv[2], pv), "bytes")

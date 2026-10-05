# Builds the "Super Mario Galaxy ONLINE" title logo from YOUR OWN copy of the
# game: takes LayoutData/TitleLogo.arc out of the disc image with DolphinTool,
# adds the ONLINE strip (online-strip.bin, our own artwork) under the logo and
# writes the result next to the mod files. Nothing of the game is shipped.
#
#   build-title.ps1 -Dolphin <Dolphin.exe> -Game <game file> -Out <TitleLogo.arc>
#
# Returns $true when the output exists afterwards.
param(
    [Parameter(Mandatory = $true)][string]$Dolphin,
    [Parameter(Mandatory = $true)][string]$Game,
    [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot

Add-Type -Language CSharp -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static class SmgOnlineTitle
{

    static uint U32(byte[] d, int o) { return (uint)(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]); }
    static int U16(byte[] d, int o) { return d[o] << 8 | d[o + 1]; }
    static void W32(byte[] d, int o, uint v) { d[o] = (byte)(v >> 24); d[o + 1] = (byte)(v >> 16); d[o + 2] = (byte)(v >> 8); d[o + 3] = (byte)v; }
    static float F32(byte[] d, int o) { byte[] b = { d[o + 3], d[o + 2], d[o + 1], d[o] }; return BitConverter.ToSingle(b, 0); }
    static void WF32(byte[] d, int o, float v) { byte[] b = BitConverter.GetBytes(v); d[o] = b[3]; d[o + 1] = b[2]; d[o + 2] = b[1]; d[o + 3] = b[0]; }

    static byte[] Yaz0(byte[] d)
    {
        if (d[0] != 'Y' || d[1] != 'a' || d[2] != 'z' || d[3] != '0') return d;
        int size = (int)U32(d, 4);
        byte[] o = new byte[size];
        int i = 16, n = 0;
        while (n < size)
        {
            int code = d[i++];
            for (int k = 0; k < 8 && n < size; k++, code <<= 1)
            {
                if ((code & 0x80) != 0) { o[n++] = d[i++]; continue; }
                int b1 = d[i], b2 = d[i + 1]; i += 2;
                int len = b1 >> 4;
                if (len == 0) len = d[i++] + 0x12; else len += 2;
                int p = n - (((b1 & 0xF) << 8) | b2) - 1;
                for (int c = 0; c < len; c++) o[n++] = o[p++];
            }
        }
        return o;
    }

    // path -> { entry offset, data offset, size }
    static Dictionary<string, int[]> Entries(byte[] d, out int dataOff)
    {
        if (d[0] != 'R' || d[1] != 'A' || d[2] != 'R' || d[3] != 'C') throw new Exception("not a RARC archive");
        dataOff = (int)U32(d, 0x0C) + 0x20;
        int nodeOff = (int)U32(d, 0x24) + 0x20, entOff = (int)U32(d, 0x2C) + 0x20, strOff = (int)U32(d, 0x34) + 0x20;
        var result = new Dictionary<string, int[]>();
        Walk(d, 0, "", nodeOff, entOff, strOff, dataOff, result);
        return result;
    }

    static string Name(byte[] d, int o)
    {
        int e = o; while (d[e] != 0) e++;
        return Encoding.ASCII.GetString(d, o, e - o);
    }

    static void Walk(byte[] d, int node, string path, int nodeOff, int entOff, int strOff, int dataOff, Dictionary<string, int[]> result)
    {
        int n = nodeOff + node * 16;
        int count = U16(d, n + 10), first = (int)U32(d, n + 12);
        for (int k = first; k < first + count; k++)
        {
            int e = entOff + k * 20;
            int flags = U16(d, e + 4);
            string name = Name(d, strOff + U16(d, e + 6));
            int off = (int)U32(d, e + 8), size = (int)U32(d, e + 12);
            if ((flags & 0x0200) != 0) { if (name != "." && name != "..") Walk(d, off, path + name + "/", nodeOff, entOff, strOff, dataOff, result); }
            else result[path + name] = new int[] { e, dataOff + off, size };
        }
    }

    public static void Build(string src, string stripPath, string dst)
    {
        byte[] orig = Yaz0(File.ReadAllBytes(src));
        byte[] strip = File.ReadAllBytes(stripPath);
        int dataOff;
        var entries = Entries(orig, out dataOff);

        // texture: the original logo with the strip appended
        int[] te = entries["timg/mytitlelogo.tpl"];
        int o = te[1], n = te[2];
        if (U32(orig, o) != 0x0020AF30 || U32(orig, o + 4) != 1) throw new Exception("unexpected logo texture");
        int ih = (int)U32(orig, o + (int)U32(orig, o + 8));
        int h = U16(orig, o + ih), w = U16(orig, o + ih + 2);
        uint fmt = U32(orig, o + ih + 4);
        int off = (int)U32(orig, o + ih + 8);
        if (w != 440 || h != 256 || fmt != 5) throw new Exception("unexpected logo texture format (already patched?)");
        // rows added under the logo: whatever the strip file holds (2 bytes per pixel)
        int Strip = strip.Length / (w * 2);
        if (Strip < 4 || Strip % 4 != 0 || strip.Length != w * Strip * 2) throw new Exception("online-strip.bin has the wrong size");
        int pixels = w * h * 2;
        byte[] tpl = new byte[off + pixels + strip.Length];
        Array.Copy(orig, o, tpl, 0, off + pixels);
        Array.Copy(strip, 0, tpl, off + pixels, strip.Length);
        int newH = h + Strip;
        tpl[ih] = (byte)(newH >> 8); tpl[ih + 1] = (byte)newH;

        // append it to the archive and repoint the file entry
        int newOff = (orig.Length + 31) & ~31;
        int total = (newOff + tpl.Length + 31) & ~31;
        byte[] d = new byte[total];
        Array.Copy(orig, d, orig.Length);
        Array.Copy(tpl, 0, d, newOff, tpl.Length);
        W32(d, te[0] + 8, (uint)(newOff - dataOff));
        W32(d, te[0] + 12, (uint)tpl.Length);
        uint grown = (uint)(total - (int)U32(orig, 4));
        W32(d, 4, (uint)total);
        W32(d, 0x10, U32(d, 0x10) + grown);     // data length
        W32(d, 0x14, U32(d, 0x14) + grown);     // MRAM size

        // layout: make the logo pane taller, keep the original art where it was
        int[] le = entries["blyt/titlelogo.brlyt"];
        int pos = le[1] + 16, end = le[1] + le[2], inside = 0;
        bool seenLogo = false;
        while (pos < end)
        {
            string tag = Encoding.ASCII.GetString(d, pos, 4);
            int size = (int)U32(d, pos + 4);
            if (tag == "pan1" || tag == "pic1")
            {
                string name = Name(d, pos + 12);
                float ty = F32(d, pos + 0x28);
                if (name == "PicLogo")
                {
                    WF32(d, pos + 0x48, F32(d, pos + 0x48) + Strip);
                    WF32(d, pos + 0x28, ty - Strip / 2.0f);
                    seenLogo = true;
                }
                else if (inside > 0) WF32(d, pos + 0x28, ty + Strip / 2.0f);   // children follow the pane centre
            }
            else if (tag == "pas1" && seenLogo) inside++;
            else if (tag == "pae1" && inside > 0) { inside--; if (inside == 0) seenLogo = false; }
            else if (seenLogo && inside == 0) seenLogo = false;
            pos += size;
        }
        File.WriteAllBytes(dst, d);
    }
}
"@

$tool = Join-Path (Split-Path $Dolphin) "DolphinTool.exe"
if (-not (Test-Path -LiteralPath $tool)) {
    Write-Host "      title screen: DolphinTool.exe not found next to Dolphin.exe, keeping the normal title." -ForegroundColor Yellow
    return $false
}

$tmp = Join-Path $env:TEMP ("smg-online-title-" + [Guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    & $tool extract -i $Game -o $tmp -s "LayoutData/TitleLogo.arc" -g -q 2>$null | Out-Null
    $src = Get-ChildItem -LiteralPath $tmp -Recurse -Filter "TitleLogo.arc" | Select-Object -First 1
    if (-not $src) { throw "could not read the title logo from the game file" }
    [SmgOnlineTitle]::Build($src.FullName, (Join-Path $here "online-strip.bin"), $Out)
    return (Test-Path -LiteralPath $Out)
} catch {
    Write-Host "      title screen: $($_.Exception.Message) - keeping the normal title." -ForegroundColor Yellow
    return $false
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

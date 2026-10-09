"""Reads ysp packs with other zip readers and compares every entry with the
manifest's SHA-256 (docs/pack.md 9.1). A reader that is not installed is
skipped with a message. Exit 0 when every reader that ran agreed.

    uv run --no-project python tests/pack/compat.py PACK [PACK...]
    uv run --no-project python tests/pack/compat.py --ypak build/pak-mingw/pack/ypak.exe --big DIR

--big DIR builds, with the given ypak, a pack of a 4.3 GB file followed by a
small one (zip64 by size, by local header offset and by directory offset)
and an appended copy of a small pack, in DIR, and checks those too.

Readers: Python's zipfile (with testzip), Info-ZIP unzip -t, bsdtar (list
and extract), Java's ZipFile and ZipInputStream, .NET's ZipFile (.NET 10
through `dotnet run`, and .NET Framework through Windows PowerShell).
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

JAVA = r"""
import java.io.*; import java.security.*; import java.util.*; import java.util.zip.*;
public class ZipCheck {
    static String hex(byte[] b) { StringBuilder s = new StringBuilder(); for (byte x : b) s.append(String.format("%02x", x)); return s.toString(); }
    static String sum(InputStream in) throws Exception { MessageDigest md = MessageDigest.getInstance("SHA-256"); byte[] b = new byte[1 << 20]; int k; while ((k = in.read(b)) > 0) md.update(b, 0, k); return hex(md.digest()); }
    public static void main(String[] a) throws Exception {
        if (a[0].equals("file")) { try (ZipFile z = new ZipFile(a[1])) { for (Enumeration<? extends ZipEntry> e = z.entries(); e.hasMoreElements();) { ZipEntry x = e.nextElement(); System.out.println(x.getName() + " " + sum(z.getInputStream(x))); } } }
        else { try (ZipInputStream z = new ZipInputStream(new BufferedInputStream(new FileInputStream(a[1])))) { ZipEntry x; while ((x = z.getNextEntry()) != null) System.out.println(x.getName() + " " + sum(z)); } }
    }
}
"""
DOTNET = r"""
using System.IO.Compression; using System.Security.Cryptography;
using var z = ZipFile.OpenRead(args[0]);
foreach (var e in z.Entries) {
    using var s = e.Open(); using var sha = SHA256.Create();
    Console.WriteLine($"{e.FullName} {Convert.ToHexString(sha.ComputeHash(s)).ToLowerInvariant()}");
}
"""
NETFX = r"""
param([string]$p)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [System.IO.Compression.ZipFile]::OpenRead($p)
foreach ($e in $z.Entries) {
  $s = $e.Open(); $sha = [System.Security.Cryptography.SHA256]::Create(); $h = $sha.ComputeHash($s); $s.Close()
  "{0} {1}" -f $e.FullName, (($h | ForEach-Object { $_.ToString('x2') }) -join '')
}
$z.Dispose()
"""


def expected(pack):
    """name -> SHA-256 from the manifest, plus the manifest itself."""
    with zipfile.ZipFile(pack) as z:
        m = z.read("ysp/manifest.json")
    man = json.loads(m)
    want = {e["name"]: e["sha256"] for e in man["entries"]}
    want["ysp/manifest.json"] = hashlib.sha256(m).hexdigest()
    return want


def parse_lines(text):
    got = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        name, _, h = line.rpartition(" ")
        got[name] = h
    return got


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=3600, **kw)


def readers(work):
    jdir = os.path.join(work, "java")
    os.makedirs(jdir, exist_ok=True)
    with open(os.path.join(jdir, "ZipCheck.java"), "w", newline="\n") as f:
        f.write(JAVA)
    ndir = os.path.join(work, "dotnet")
    os.makedirs(ndir, exist_ok=True)
    with open(os.path.join(ndir, "check.cs"), "w", newline="\n") as f:
        f.write(DOTNET)
    ps1 = os.path.join(work, "netfx.ps1")
    with open(ps1, "w", newline="\n") as f:
        f.write(NETFX)
    bsdtar = shutil.which("bsdtar") or (r"C:\Windows\System32\tar.exe" if os.name == "nt" else None)
    out = []

    def py(pack):
        got = {}
        with zipfile.ZipFile(pack) as z:
            if z.testzip() is not None:
                raise RuntimeError("testzip found a bad CRC")
            for i in z.infolist():
                h = hashlib.sha256()
                with z.open(i) as fh:
                    while True:
                        b = fh.read(1 << 20)
                        if not b:
                            break
                        h.update(b)
                got[i.filename] = h.hexdigest()
        return got
    out.append(("Python zipfile", py))
    if shutil.which("unzip"):
        def uz(pack):
            r = run(["unzip", "-t", pack])
            if r.returncode != 0:
                raise RuntimeError("unzip -t exit %d: %s" % (r.returncode, r.stdout[-300:] + r.stderr[-300:]))
            return None   # CRC only
        out.append(("Info-ZIP unzip -t", uz))
    if bsdtar and os.path.exists(bsdtar):
        def bt(pack):
            d = tempfile.mkdtemp(dir=work)
            r = run([bsdtar, "-xf", pack, "-C", d])
            if r.returncode != 0:
                raise RuntimeError("bsdtar exit %d: %s" % (r.returncode, r.stderr[-300:]))
            got = {}
            for root, _, files in os.walk(d):
                for fn in files:
                    p = os.path.join(root, fn)
                    with open(p, "rb") as fh:
                        h = hashlib.sha256()
                        for b in iter(lambda: fh.read(1 << 20), b""):
                            h.update(b)
                    got[os.path.relpath(p, d).replace(os.sep, "/")] = h.hexdigest()
            shutil.rmtree(d, ignore_errors=True)
            return got
        out.append(("bsdtar", bt))
    if shutil.which("java"):
        jf = os.path.join(jdir, "ZipCheck.java")
        out.append(("Java ZipFile", lambda pack: parse_lines(run(["java", jf, "file", pack]).stdout)))
        out.append(("Java ZipInputStream", lambda pack: parse_lines(run(["java", jf, "stream", pack]).stdout)))
    if shutil.which("dotnet"):
        cs = os.path.join(ndir, "check.cs")
        out.append((".NET 10 ZipFile", lambda pack: parse_lines(run(["dotnet", "run", cs, "--", os.path.abspath(pack)], cwd=ndir).stdout)))
    if os.name == "nt" and shutil.which("powershell"):
        out.append((".NET Framework ZipFile", lambda pack: parse_lines(
            run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ps1, os.path.abspath(pack)]).stdout)))
    return out


def check(pack, rds, appended):
    want = expected(pack)
    bad = 0
    for name, fn in rds:
        if appended and name == "Java ZipInputStream":
            print("  %-24s skipped: a streaming reader cannot start inside a program" % name)
            continue
        try:
            got = fn(pack)
        except Exception as e:   # a reader's failure is a finding
            print("  %-24s FAILED: %s" % (name, e))
            bad += 1
            continue
        if got is None:
            print("  %-24s ok (CRC-32 of every entry)" % name)
            continue
        miss = [n for n in want if got.get(n) != want[n]]
        if miss or len(got) != len(want):
            print("  %-24s FAILED: %d of %d entries differ or are missing (first: %s)" % (name, len(miss), len(want), miss[:1]))
            bad += 1
        else:
            print("  %-24s ok (%d entries, SHA-256 equal)" % (name, len(want)))
    return bad


def build_big(ypak, d, small_pack):
    os.makedirs(d, exist_ok=True)
    big = os.path.join(d, "film.bin")
    if not os.path.exists(big) or os.path.getsize(big) != 4300000000:
        block = bytes((i * 2654435761 >> 24) & 255 for i in range(1 << 20))
        with open(big, "wb") as f:
            left = 4300000000
            while left:
                k = min(left, len(block))
                f.write(block[:k])
                left -= k
    with open(os.path.join(d, "after.txt"), "wb") as f:
        f.write(b"after the 4.3 GB entry\n")
    desc = {"format": "ysp-pack-source", "version": 1, "resources": [
        {"kind": "file", "name": "a_film.bin", "source": "film.bin"},
        {"kind": "file", "name": "z_after.txt", "source": "after.txt"}]}
    with open(os.path.join(d, "big.json"), "w", newline="\n") as f:
        json.dump(desc, f)
    out = os.path.join(d, "big.ysppak")
    r = run([ypak, "build", os.path.join(d, "big.json"), "-o", out])
    if r.returncode:
        raise SystemExit("ypak build failed: " + r.stderr)
    with open(os.path.join(d, "player.bin"), "wb") as f:
        f.write(b"MZ" + bytes(10000))
    app = os.path.join(d, "appended.bin")
    r = run([ypak, "append", os.path.join(d, "player.bin"), small_pack, "-o", app])
    if r.returncode:
        raise SystemExit("ypak append failed: " + r.stderr)
    return [out, app]


def main():
    args = sys.argv[1:]
    ypak = None
    big = None
    packs = []
    i = 0
    while i < len(args):
        if args[i] == "--ypak":
            ypak = os.path.abspath(args[i + 1]); i += 2
        elif args[i] == "--big":
            big = args[i + 1]; i += 2
        else:
            packs.append(args[i]); i += 1
    if big:
        if not ypak or not packs:
            raise SystemExit("--big needs --ypak and a small pack to append")
        packs += build_big(ypak, big, packs[0])
    work = tempfile.mkdtemp(prefix="ypak-compat-")
    try:
        rds = readers(work)
        bad = 0
        for p in packs:
            print("%s (%d bytes)" % (p, os.path.getsize(p)))
            bad += check(p, rds, appended=not open(p, "rb").read(2) == b"PK")
        print("compat: %s" % ("ok" if not bad else "%d failures" % bad))
        return 1 if bad else 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

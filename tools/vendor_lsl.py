#!/usr/bin/env python3
"""Fetch or verify third_party/liblsl/<platform>/: liblsl for ysp/net.h's loopback test.

    uv run --no-project python tools/vendor_lsl.py            # verify (default)
    uv run --no-project python tools/vendor_lsl.py --write    # fetch into third_party/liblsl/<platform>/
    uv run --no-project python tools/vendor_lsl.py --write --cache DIR   # keep the archive between runs (CI)
    uv run --no-project python tools/vendor_lsl.py --platform jammy_amd64 --write

ysp/net.h never links liblsl; it loads it at run time. This copy is for the
tests only: tests/loopback/net_loopback.c loads it, and tests/compile/lsl_abi.cpp
checks the header's declarations against liblsl's own lsl_c.h. Nothing here
is redistributed.

The pin is liblsl v1.17.7 (MIT; https://github.com/sccn/liblsl/releases/tag/v1.17.7),
the newest release that GitHub does not mark as a pre-release (2026-04-22;
v1.18.0.b1 to b5 are betas) and that has Windows x64 and Linux x64 builds.
The archive is downloaded from the release by its exact URL and compared
with the SHA-256 below (the value GitHub's release API states for the
asset); then only the shared library and the C headers are taken out, each
by its exact path and each compared with its own SHA-256. Nothing else in
the archive is written, and no archive member names a path. Standard
library only.

The platform is the host's by default: win_amd64 on Windows, and on Linux
noble_amd64 (Ubuntu 24.04, CI's ubuntu-latest) or jammy_amd64 (Ubuntu 22.04,
older glibc) by /etc/os-release. The macOS build is a .pkg installer, which
the standard library cannot open; macOS is not pinned.
"""
import argparse
import hashlib
import io
import os
import sys
import tarfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "third_party", "liblsl")
TAG = "v1.17.7"
URL = "https://github.com/sccn/liblsl/releases/download/%s/%s"

# Per platform: the archive, its SHA-256, and the files taken out of it:
# member path -> (name under third_party/liblsl/<platform>/, SHA-256).
H_LINUX = {
    "include/lsl_c.h": "7191f28ec454149aebed65fffa7257733b7574adffa3a5e465fe03a2df835e03",
    "include/lsl/common.h": "0e4c6f653c77de0c3b05f8504c0b49e1dba36cd3bad2c370f11c1f9537b84d17",
    "include/lsl/inlet.h": "66bf7869b1e44b6ab0221ae23103a3f8ebeb390092779c2923f4661b8bfabd5a",
    "include/lsl/outlet.h": "6130b44689bb949c9f49617841658aca0e1842bb8172adb113d1079b97f7e3b3",
    "include/lsl/resolver.h": "5d05ffd8eb955577ff926f77233d26acf859fa1b144fea7073e9f0098bb6e28b",
    "include/lsl/streaminfo.h": "627b1d0ede33d86ad6254f894564189edb0ae69d3b9bfa75d55acb349cd9132c",
    "include/lsl/types.h": "394ac8258c9af84a17ebe8b78ad4b07e608cc715f28dd6f9b04e0d206bc306d6",
    "include/lsl/xml.h": "4ade06054c008560dd5d18d83c4bd3480bf22919036119ec3cd2e9caf74ed3fc",
}
PLATFORMS = {
    "win_amd64": {
        "archive": "liblsl-1.17.7-Win_amd64.zip",
        "sha256": "1285c4846f705108d417f5b7e57727f7e864941692d936fa18f8e7ab9b7112e1",
        "lib": ("bin/lsl.dll", "lsl.dll", "8156d0021794135ce217821cae0e99912753d86d8519e349756d13d99e0292ff"),
        # CRLF in the Windows archive, so other hashes than Linux's
        "headers": {
            "include/lsl_c.h": "c1cd8a04d88d68b8591d4dfb7e72f2e79698baf7bfb547e15cd8f99f120cfd3b",
            "include/lsl/common.h": "162111952f549c70e8d6561bf91207e46f37b95288c6c7d12f732ed97ce6d1a3",
            "include/lsl/inlet.h": "5232edbf372d8c76a916fe5eacfa1c2c53e20d354e66cd57a18b650e0aa8785f",
            "include/lsl/outlet.h": "8e526d9a1653082bc6b7a47978eca4152cd15f46355f2c730d69fc8c02dd8364",
            "include/lsl/resolver.h": "f910c2e521dda9b7e18ffb6a493d0f9c69510373b7acb95b9ebe89349d4d7894",
            "include/lsl/streaminfo.h": "77494ed267e30d90bdc09bd8f538287bb7c55f5dce6681834c1e5956adb1ce2c",
            "include/lsl/types.h": "410e40709f19ce581888c798e0831ba634e76444d5357269fff8561e0e7b5f70",
            "include/lsl/xml.h": "25831f499deead2a4c447de1cae88ad1cd0ccc98334178c278bb52f584c7c222",
        },
    },
    "noble_amd64": {
        "archive": "liblsl-1.17.7-noble_amd64.tar.gz",
        "sha256": "91df230c4ad8daa9a4c0ef78b63b777df5c293f774e4d06e474167620b7e9de7",
        "lib": ("lib/liblsl.so.1.17.7", "liblsl.so", "5d05f8c9bee4cbd7eb31df509a6fb083452ab7fe3292045617966e351312c0fa"),
        "headers": H_LINUX,
    },
    "jammy_amd64": {
        "archive": "liblsl-1.17.7-jammy_amd64.tar.gz",
        "sha256": "a73dfca12aaffe65d22599032fa6ba683e9a54e2f8e4212cfdb05d5854b86bec",
        "lib": ("lib/liblsl.so.1.17.7", "liblsl.so", "1194761815c5713f4550f29e58272ab69c127e94eedb40be04930a94a593d0d3"),
        "headers": H_LINUX,
    },
}


def host_platform():
    if sys.platform == "win32":
        return "win_amd64"
    if sys.platform.startswith("linux"):
        try:
            with open("/etc/os-release", encoding="utf-8") as f:
                rel = f.read()
        except OSError:
            rel = ""
        return "jammy_amd64" if "VERSION_CODENAME=jammy" in rel else "noble_amd64"
    return None


def wanted(p):
    """Member path inside the archive's top folder -> (output name, SHA-256)."""
    out = {p["lib"][0]: (p["lib"][1], p["lib"][2])}
    for m, h in p["headers"].items():
        out[m] = (m[len("include/"):], h)
    return out


def fetch_archive(p, cache):
    name, want = p["archive"], p["sha256"]
    if cache:
        cp = os.path.join(cache, TAG, name)
        if os.path.exists(cp):
            with open(cp, "rb") as f:
                data = f.read()
            if hashlib.sha256(data).hexdigest() == want:
                return data
    with urllib.request.urlopen(URL % (TAG, name), timeout=120) as r:
        data = r.read()
    got = hashlib.sha256(data).hexdigest()
    if got != want:
        raise SystemExit("error: %s has SHA-256 %s, expected %s" % (name, got, want))
    if cache:
        os.makedirs(os.path.join(cache, TAG), exist_ok=True)
        with open(os.path.join(cache, TAG, name), "wb") as f:
            f.write(data)
    return data


def members(p, data):
    """The wanted members' bytes, read by exact name; nothing else."""
    top = p["archive"].rsplit(".zip", 1)[0].rsplit(".tar.gz", 1)[0] + "/"
    out = {}
    if p["archive"].endswith(".zip"):
        z = zipfile.ZipFile(io.BytesIO(data))
        for m in wanted(p):
            out[m] = z.read(top + m)
    else:
        t = tarfile.open(fileobj=io.BytesIO(data), mode="r:gz")
        for m in wanted(p):
            info = t.getmember(top + m)
            if not info.isfile():
                raise SystemExit("error: %s in %s is not a plain file" % (m, p["archive"]))
            out[m] = t.extractfile(info).read()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--write", action="store_true", help="download and write third_party/liblsl/<platform>/")
    ap.add_argument("--cache", help="a folder that keeps the downloaded archive between runs")
    ap.add_argument("--platform", choices=sorted(PLATFORMS), help="default: the host's")
    a = ap.parse_args()
    plat = a.platform or host_platform()
    if plat not in PLATFORMS:
        print("no pinned liblsl for this platform (%s)" % sys.platform)
        return 1
    p = PLATFORMS[plat]
    dest = os.path.join(BASE, plat)
    want = wanted(p)
    bad = 0
    if a.write:
        got = members(p, fetch_archive(p, a.cache))
        for m, (outname, h) in sorted(want.items()):
            data = got[m]
            if hashlib.sha256(data).hexdigest() != h:
                print("error: %s in %s has SHA-256 %s, expected %s" % (m, p["archive"], hashlib.sha256(data).hexdigest(), h))
                bad += 1
                continue
            path = os.path.join(dest, *outname.split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
        if bad:
            return 1
    for m, (outname, h) in sorted(want.items()):
        path = os.path.join(dest, *outname.split("/"))
        if not os.path.exists(path):
            print("missing: %s (run with --write)" % os.path.relpath(path, ROOT))
            bad += 1
            continue
        with open(path, "rb") as f:
            got = hashlib.sha256(f.read()).hexdigest()
        if got != h:
            print("differs: %s has SHA-256 %s, expected %s" % (os.path.relpath(path, ROOT), got, h))
            bad += 1
    if bad:
        return 1
    print("ok: liblsl %s %s, %d files in %s" % (TAG, plat, len(want), os.path.relpath(dest, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

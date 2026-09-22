import os
import sys
from setuptools import setup, Extension

HERE = os.path.abspath(os.path.dirname(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

# psy_serial.h lives at the repo root; that copy always wins in a dev tree so a
# stale staged copy cannot shadow it. Isolated builds (sdist / cibuildwheel see
# only this directory) get the header staged next to this file by CI.
HEADER_DIR = REPO_ROOT if os.path.exists(os.path.join(REPO_ROOT, "psy_serial.h")) else HERE

extra_compile_args = []
extra_link_args = []
libraries = []
if sys.platform == "win32":
    # Port enumeration uses SetupAPI and the registry. MSVC picks both up from
    # a #pragma comment in the implementation; naming them here also covers a
    # MinGW build, which has no such pragma.
    libraries += ["setupapi", "advapi32"]
else:
    # The async-pulse worker uses pthreads on Linux/macOS.
    extra_compile_args.append("-pthread")
    extra_link_args.append("-pthread")

# Target the CPython 3.8+ stable ABI (Limited API): one .abi3.so works across
# Python versions. The macro must match Py_LIMITED_API in the C source.
PY_LIMITED = 0x03080000

# Dotted name: the extension is built as psy/serial.abi3.so. There is no
# psy/__init__.py anywhere, so `psy` is a PEP 420 implicit namespace package and
# psy-parallel and psy-serial can be installed together or separately.
ext = Extension(
    name="psy.serial",
    sources=[os.path.join(HERE, "psy_serial_ext.c")],
    include_dirs=[HEADER_DIR],  # for "psy_serial.h"
    define_macros=[("Py_LIMITED_API", hex(PY_LIMITED))],
    py_limited_api=True,
    libraries=libraries,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
)

setup(
    name="psy-serial",
    version="0.2.0",  # tracks the psy_serial.h version
    description="Serial-port byte I/O for trigger and response boxes",
    long_description="CPython binding for the psy_serial single-header C library.",
    ext_modules=[ext],
    # `psy` is a PEP 420 namespace: no __init__.py, nothing to package. An
    # explicit empty list also stops setuptools auto-discovery from treating
    # the psy/ directory (kept for in-place builds) as a package.
    packages=[],
    python_requires=">=3.8",
    # Build a cp38-abi3 wheel rather than a version-specific one.
    options={"bdist_wheel": {"py_limited_api": "cp38"}},
)

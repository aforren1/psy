import os
import sys
from setuptools import setup, Extension

HERE = os.path.abspath(os.path.dirname(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

# psy_parallel.h lives at the repo root; that copy always wins in a dev tree so a
# stale staged copy cannot shadow it. Isolated builds (sdist / cibuildwheel see
# only this directory) get the header staged next to this file by CI. One
# directory serves both cases: psy_parallel.h includes psy_rt.h, and the two
# headers are always staged together.
HEADER_DIR = REPO_ROOT if os.path.exists(os.path.join(REPO_ROOT, "psy_parallel.h")) else HERE

extra_compile_args = []
extra_link_args = []
if sys.platform != "win32":
    # The async-pulse worker uses pthreads on Linux/macOS.
    extra_compile_args.append("-pthread")
    extra_link_args.append("-pthread")

# Target the CPython 3.8+ stable ABI (Limited API): one .abi3.so works across
# Python versions. The macro must match Py_LIMITED_API in the C source.
PY_LIMITED = 0x03080000

# Dotted name: the extension is built as psy/parallel.abi3.so. There is no
# psy/__init__.py anywhere, so `psy` is a PEP 420 implicit namespace package and
# psy-parallel and psy-serial can be installed together or separately.
ext = Extension(
    name="psy.parallel",
    sources=[os.path.join(HERE, "psy_parallel_ext.c")],
    include_dirs=[HEADER_DIR],  # for "psy_parallel.h" and the "psy_rt.h" it includes
    define_macros=[("Py_LIMITED_API", hex(PY_LIMITED))],
    py_limited_api=True,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
)

# Metadata (name, version, description, readme, requires-python, license) lives
# in pyproject.toml only. setuptools >= 77 ignores a setup.py copy of a field
# pyproject.toml also declares, and warns about it, so two sources of truth here
# would silently drift. This call carries only what pyproject.toml cannot say:
# the C extension and the abi3 wheel tag.
setup(
    ext_modules=[ext],
    # `psy` is a PEP 420 namespace: no __init__.py, nothing to package. An
    # explicit empty list also stops setuptools auto-discovery from treating
    # the psy/ directory (kept for in-place builds) as a package.
    packages=[],
    # Build a cp38-abi3 wheel rather than a version-specific one.
    options={"bdist_wheel": {"py_limited_api": "cp38"}},
)

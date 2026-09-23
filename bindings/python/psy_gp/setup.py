import os
import sys
from setuptools import setup, Extension

HERE = os.path.abspath(os.path.dirname(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

# psy_gp.h lives at the repo root; that copy always wins in a dev tree so a
# stale staged copy cannot shadow it. Isolated builds (sdist / cibuildwheel see
# only this directory) get the header staged next to this file by CI. One
# directory serves both cases: the async layer makes psy_gp.h include
# psy_rt.h, and the two headers are always staged together.
HEADER_DIR = REPO_ROOT if os.path.exists(os.path.join(REPO_ROOT, "psy_gp.h")) else HERE

extra_compile_args = []
extra_link_args = []
libraries = []
if sys.platform != "win32":
    # The async layer's pump thread uses pthreads on Linux/macOS, and the
    # library needs libm there.
    extra_compile_args.append("-pthread")
    extra_link_args.append("-pthread")
    libraries.append("m")

# Target the CPython 3.8+ stable ABI (Limited API): one .abi3.so works across
# Python versions. The macro must match Py_LIMITED_API in the C source.
PY_LIMITED = 0x03080000

# Dotted name: the extension is built as psy/gp.abi3.so. There is no
# psy/__init__.py anywhere, so `psy` is a PEP 420 implicit namespace package and
# psy-gp installs next to the other psy-* distributions without owning it.
ext = Extension(
    name="psy.gp",
    sources=[os.path.join(HERE, "psy_gp_ext.c")],
    include_dirs=[HEADER_DIR],  # for "psy_gp.h" and the "psy_rt.h" it includes
    # PSYGP_ASYNC compiles the async layer (and psy_rt.h's pump) in; the binding
    # always ships it, because a Python trial loop is where it pays.
    define_macros=[("Py_LIMITED_API", hex(PY_LIMITED)), ("PSYGP_ASYNC", "1")],
    py_limited_api=True,
    libraries=libraries,
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
    # the psy/ directory (kept for in-place builds) or tests/ as a package.
    packages=[],
    # Build a cp38-abi3 wheel rather than a version-specific one.
    options={"bdist_wheel": {"py_limited_api": "cp38"}},
)

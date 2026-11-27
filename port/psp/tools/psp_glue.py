#!/usr/bin/env python3
"""gen_link_glue.py for the PSP link, without changing upstream tools.

link_catalog.host_runtime_names() decides which undefined names the host C
library resolves (memmove, the soft-float __adddf3 family, ...).  Its library
list knows Emscripten and i386 Linux; this adds pspdev's newlib, libm and
libgcc, then runs gen_link_glue.py with the same arguments.  PATH must hold
llvm-nm (port/psp/CMakeLists.txt passes the clang that builds the game).
"""
import glob
import os
import runpy
import sys

TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools")
sys.path.insert(0, os.path.abspath(TOOLS))
import link_catalog  # noqa: E402

psp = os.path.abspath(os.environ.get("PSPDEV", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "private", "toolchains", "pspdev")))
link_catalog.HOST_LIBS += [f"{psp}/psp/lib/libc.a", f"{psp}/psp/lib/libm.a"]
link_catalog.HOST_LIBS += glob.glob(f"{psp}/lib/gcc/psp/*/libgcc.a")
# P101: the -DMVC2_PSP_HOTPROF=ON build's hooks (port/psp/host/psp_hot.c)
_runtime = link_catalog.host_runtime_names
link_catalog.host_runtime_names = lambda: _runtime() | {"__cyg_profile_func_enter", "__cyg_profile_func_exit"}

sys.argv[0] = os.path.join(os.path.abspath(TOOLS), "gen_link_glue.py")
runpy.run_path(sys.argv[0], run_name="__main__")

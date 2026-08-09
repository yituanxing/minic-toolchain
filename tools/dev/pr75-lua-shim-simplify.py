#!/usr/bin/env python3
from pathlib import Path

path = Path("tests/external/lua/probe.sh")
text = path.read_text()
old = '''cat >"$include/signal.h" <<'EOF'\n#ifndef MINIC_LUA_SIGNAL_H\n#define MINIC_LUA_SIGNAL_H\ntypedef int sig_atomic_t;\n#define SIGINT 2\n#define SIG_DFL ((void (*)(int))0)\nvoid (*signal(int signal_number, void (*handler)(int)))(int);\n#endif\nEOF\n'''
new = '''cat >"$include/signal.h" <<'EOF'\n#ifndef MINIC_LUA_SIGNAL_H\n#define MINIC_LUA_SIGNAL_H\ntypedef int sig_atomic_t;\ntypedef void (*minic_lua_sighandler_t)(int);\n#define SIGINT 2\n#define SIG_DFL ((minic_lua_sighandler_t)0)\nminic_lua_sighandler_t signal(int signal_number, minic_lua_sighandler_t handler);\n#endif\nEOF\n'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected Lua signal shim count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged equivalent typedef-based Lua signal shim")

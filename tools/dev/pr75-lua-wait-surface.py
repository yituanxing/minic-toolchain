#!/usr/bin/env python3
from pathlib import Path

path = Path("tests/external/lua/probe.sh")
text = path.read_text()
anchor = '''cat >"$include/sys/types.h" <<'EOF'
#ifndef MINIC_LUA_SYS_TYPES_H
#define MINIC_LUA_SYS_TYPES_H
typedef long ssize_t;
typedef long off_t;
#endif
EOF
'''
addition = anchor + '''
cat >"$include/sys/wait.h" <<'EOF'
#ifndef MINIC_LUA_SYS_WAIT_H
#define MINIC_LUA_SYS_WAIT_H
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#define WTERMSIG(status) ((status) & 0x7f)
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#endif
EOF
'''
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"Lua sys/types surface anchor: expected 1 match, found {count}")
path.write_text(text.replace(anchor, addition, 1))
print("staged Lua Linux sys/wait status macros")

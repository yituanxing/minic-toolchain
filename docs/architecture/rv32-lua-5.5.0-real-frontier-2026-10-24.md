# RV32I / ILP32 original Lua 5.5.0 runtime frontier

The fully qualified RV32 original tinyAES → cJSON 1.7.19 → zlib 1.3.1
and upstream minigzip path established MiniC machine ABI and
authentic Picolibc syscall environment. This next integration probes
the **unmodified official Lua 5.5.0 source archive**, SHA-256
`57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d`.

One CI shard provisions Picolibc once and compiles all 34 real Lua
translation units with GNU preprocess, full independent GCC C and
MiniC + GNU AS + MiniAS. Any unsupported TU is recorded and the
gate fails; no conditional compilation removes Lua functions, and no
C translation unit falls back to GCC in a MiniC interpreter.

The portable default Lua configuration is valid upstream and maps
to real RV32 Picolibc; no fake Linux/glibc declarations or manual Lua
source edits are introduced. If all TUs assemble, use the original
`lua.c` interpreter with all MiniC Lua objects and authentic
Picolibc to execute the same real table/coroutine/math/string pressure
script as the independent GNU C reference, byte-exact stdout/stderr.

This starts as a strict acceptance gate, **not** a claim of Lua support.
Only a green original interpreter QEMU differential qualifies it.
Follow-up: original `luac.c` executable, bytecode binary differential
and Lua execution of source/bytecode once initial interpreter qualifies.

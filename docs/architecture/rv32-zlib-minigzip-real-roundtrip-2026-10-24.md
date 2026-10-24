# RV32I upstream minigzip round-trip acceptance

Beyond zlib's 15 unchanged translation units and the untouched
`test/example.c`, now attempt another distinct real-world executable:
the original upstream `test/minigzip.c` at pinned zlib 1.3.1 commit
`51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`.

No generated or simplified minigzip C source is substituted. A single
RV32I/ILP32 translation unit is GNU-preprocessed once; GNU C builds an
independent oracle, and MiniC builds the entire minigzip program.
MiniC's output is assembled through GNU AS and MiniAS independently.

All three executables link the 15 same-mode zlib object files, the
same authentic Picolibc and Linux syscall glue, and run in QEMU.
The test compresses a deterministic 32768-byte binary file, compares
the actual compressed gzip bytes to GNU C's reference, runs the
original minigzip -d program, verifies that every byte matches the
original payload, and checks the expected gzip file cleanup.
This is an operational I/O/compression oracle beyond 'program starts'.
It must return a real success marker before capability is claimed.

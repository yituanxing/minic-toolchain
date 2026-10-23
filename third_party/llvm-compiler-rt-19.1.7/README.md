# Pinned LLVM compiler-rt software-floating runtime for RV64I/LP64

Upstream source: https://github.com/llvm/llvm-project/tree/llvmorg-19.1.7/compiler-rt/lib/builtins
Tag: `llvmorg-19.1.7`; the runtime C/.h/.inc sources in this directory are copied
**unmodified** from that upstream tag. Four standard freestanding headers
in `freestanding-clang/` are independently copied **unmodified** from
`clang/lib/Headers/{float,limits,stdbool,stdint}.h` at the same LLVM tag.
They avoid depending on cross-glibc LP64 multilib headers (Debian's default
cross sysroot may have LP64D-only `gnu/stubs`). They are compilation
headers, not private floating arithmetic implementations. The `LICENSE.TXT` is copied from
`compiler-rt/LICENSE.TXT`: Apache-2.0 with LLVM exceptions.

This is third-party compiler runtime code, not MiniC's own IEEE-754
algorithm. The optional `make runtime-rv64i` target now combines
the pinned GNU libgcc 14.2.0 64-bit integer arithmetic entry points
with compiler-rt's `__{add,sub,mul,div}{sf,df}3` implementations,
leading-zero helpers and default rounding-mode helper.

The combined archive must be compiled for `-march=rv64i -mabi=lp64`
to ensure it does not use M/A/F/D/C or the toolchain's default
incompatible LP64D multilib. It is an opt-in provider for `--rtlib=PATH`,
not a new implicit prerequisite for normal MiniC builds.

This is **not** a complete libgcc replacement. It does not yet implement
all float comparisons, integer/fp conversions, binary128, extended
rounding-environment semantics, or other compiler builtins. Separate
target legalization and runtime symbol checks must reject or qualify
all such uses.

# Core → MachineIR production migration gates

These tests deliberately separate two facts:

1. **Golden A/B:** the current compiler still produces byte-for-byte the same
   GNU `-S` assembly as the pre-MIR direct backend from commit
   `96f2228403d1930ab1adae8d7383ab1cdf84a9f6`.
2. **Production MIR:** the compiler actually invokes
   `minic_machine_gnu_asm_emit_body` while writing the requested `.s` file.
   Merely linking that symbol or printing `MIR` on stdout is not evidence.

Commands from the repository root:

```sh
make MODE=debug check-a64-loong-mir-golden
make MODE=debug check-five-machine-mir-golden
make MODE=debug check-a64-loong-mir-production
```

The first two are safe before migration (4 cases × A64/Loong; 19 cases across
five architectures, with the existing x86 source-ASM exclusion). The third is
**intentionally expected to fail** until both production Core→MachineIR
selectors replace the old Core→FILE* path. It must stay out of `check-fast`
until then. A Linux host, normal GNU C preprocessor, `nm`, and `LD_PRELOAD` are
the only dependencies; neither cross compiler nor QEMU is required.

Cases cover stack/ABI frame and 10-argument call; control flow, switch,
globals/relocation; entry-block backedge; accepted inline assembler; and
rejection of an operand-less source ASM template referring to `%0`.

Golden hashes are SHA-256 of complete `.s` bytes (not whitespace-normalized).
To investigate an intentional output change, build the old compiler separately
and invoke `probe_machine_ir.py --baseline-cc <old-minic-cc> ...`; the script
will print a bounded unified diff on a mismatch. Hash updates need review of
both the diff and the matching functional checks, not blind regeneration.

The MIR witness runs in the real compiler process. It observes the *live*
call stack at libc output calls, resolves the typed writer's actual PIE base
and symbol range, then matches the stream's file identity to the exact `-o`
output path. A throwaway test stream is insufficient. The witness marker is a
private file created only after the check; the script never trusts compiler
stdout or assembly comments as evidence of a typed production path.

# Core → MachineIR production migration gates

These tests deliberately separate two facts:

1. **Golden A/B:** the current compiler still produces byte-for-byte the same
   GNU `-S` assembly as the pre-MIR direct backend. The original four fixtures
   were pinned at `96f2228403d1930ab1adae8d7383ab1cdf84a9f6`; the
   ABI/FP/unaligned/indirect extensions were captured at
   `b7c1b373b841905989988fc1a61e7698b05687b9` before production switch.
2. **Production MIR:** the compiler actually invokes
   `minic_machine_gnu_asm_emit_body` while writing the requested `.s` file.
   Merely linking that symbol or printing `MIR` on stdout is not evidence.

Commands from the repository root:

```sh
make MODE=debug check-a64-loong-mir-golden
make MODE=debug check-five-machine-mir-golden
make MODE=debug check-a64-loong-mir-production
make MODE=debug check-five-machine-mir-production
make MODE=debug check-x86-machine-runtime

# One focused fixture without a broad C/assembly recompilation:
python3 tests/compiler/mir-migration/probe_machine_ir.py \
  --mode golden --targets five --case packed_memory \
  --minic-cc build/debug/bin/minic-cc
```

The golden gates are safe before migration (10 fixtures × A64/Loong; across
all five targets there are 47 exact byte-golden positives and 3 explicit
legacy-backend rejection checks). The x86 executable gate compares nine
actual host-native program exit codes/output with binaries linked from MiniC
GNU assembly using that same host GCC; legacy x86 source ASM stays explicitly
unsupported. Both production gates are **intentionally
expected to fail** until the selected targets replace the old Core→FILE*
path. In five-target production mode, the same 47 legacy-supported cases
must reach the real typed writer and retain their semantic shapes. The three
legacy-unsupported cases must still reject cleanly; extending them is not
a prerequisite for migrating the old backend. Keep production out of
`check-fast` until then. A Linux host, GNU C preprocessor, `nm`, and
`LD_PRELOAD` suffice; neither cross compiler nor QEMU is required.

Cases cover:

- Integer stack/ABI frame with a 10-argument call.
- Switch/CFG/global relocations and an explicit Core entry-block backedge.
- Nested aggregate by-value arguments, hidden structure returns and copies.
- FP32/FP64 arithmetic, conversion, comparison and 10-argument mixed ABI.
- Packed unaligned word access, bit fields, volatile storage and record copy.
- Computed goto (`&&label`, `goto *p`), function-pointer calls and switch.
- Variadic ABI cursor (`va_start`/`va_arg`/`va_end`), nine stack/register
  argument values, and an indirect variadic function call.
- Signed/unsigned 128-bit arithmetic, shifts, casts and by-value ABI;
  the direct backend's RV32 ILP32 frame and LoongArch64 Core-operation gaps
  are **verified rejection cases**, not silent missing test rows.
- Accepted source ASM/empty memory barriers (RV32/RV64/A64/Loong) and an
  operand-less forged `%0` template rejected on *all five* architectures.
  The old x86 backend also rejects the positive source ASM case, so the
  migration gate preserves that refusal until a separate enhancement.

Each fixture is preprocessed once with the host GNU C preprocessor, then
compiled by the same `minic-cc` for each target. GOLDEN checks byte-for-byte
hashes, semantic-shape markers and dangling labels. PRODUCTION requires
the real typed writer for all 47 supported cases, plus semantic-shape/CFG
checks; a different valid machine instruction sequence need not hash to the
old textual output. Both modes demand a clean failure with no partial file
for the three legacy-unsupported cases. Typed output still needs GNU
assembly/linking and target runtime/reference proof before Linux signoff.

Golden hashes are SHA-256 of complete `.s` bytes (not whitespace-normalized).
To investigate an intentional output change, build the old compiler separately
and invoke `probe_machine_ir.py --baseline-cc <old-minic-cc> ...`; the script
will print a bounded unified diff on a mismatch. Hash updates need review of
both the diff and the matching functional checks, not blind regeneration.
Stale `.s` output files are unlinked before each invocation and duplicate or
malformed golden entries are rejected.

The MIR witness runs in the real compiler process. It observes the *live*
call stack at libc output calls, resolves the typed writer's actual PIE base
and symbol range, then matches the stream's file identity to the exact `-o`
output path. A throwaway test stream is insufficient. The witness marker is a
private file created only after the check; the script never trusts compiler
stdout or assembly comments as evidence of a typed production path.

These are **compiler assembly differential gates**, not final runtime or
Linux-kernel proof. The additional x86 native runtime gate covers only the
host x86 target. After a production switch, the other architectures still
need ordinary program/link/QEMU and then Linux results. A passing golden alone
cannot claim a runnable kernel.

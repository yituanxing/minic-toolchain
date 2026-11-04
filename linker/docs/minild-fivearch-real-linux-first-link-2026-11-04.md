# MiniLD five-architecture Linux first-link frontier (2026-11-04)

## Acceptance boundary

These are distinct, independently evidenced stages:

1. GNU GCC/GAS/BFD Linux 6.6.143 Kbuild and GNU-linked guest/QEMU control.
2. SHA-authenticated original GNU input object pool and the first successful
   `.tmp_vmlinux.kallsyms1` GNU link command, including its argv and cwd.
3. GNU BFD and MiniLD replay of **that same** `kallsyms1` command and frozen
   inputs, with only the output path changed.
4. MiniLD-produced final `vmlinux`/`Image`, guest P0/P1 and sustained QEMU
   runtime. **This fourth stage has not been certified in this run.**

No successful host toy, Script/GC fixture, cached input verification or GNU
control alone is a MiniLD Linux PASS.

## Genuine GNU Linux producer runs

The four-architecture producer is GitHub Actions
[`41848589048`](https://github.com/yituanxing/minic-toolchain/actions/runs/41848589048)
at commit `a2971c39371a4f9227748cc1120998ffe8c782cc`.

| Target | GNU Linux/QEMU control | Frozen `.o`/`.a` count | First MiniLD attempt before this change |
| --- | --- | ---: | --- |
| RV32 ILP32 | PASS, P1 12/12 | 2,416 | `rv32-unsupported-64bit-arithmetic` |
| x86-64 | PASS | 3,587 | Replay harness erroneously required a direct `.o` alongside `vmlinux.a` |
| AArch64 LP64 | PASS | 5,176 | `unsupported-option:-EL` |
| LoongArch64 LP64 | PASS | 3,464 | `unsupported-option:-n` |

Each control completed GNU Kbuild and existing target QEMU tests, captured
and independently replayed GNU's first Linux linker command, and saved a
matching GNU output tree/cache plus original LD argv and SHA manifests.
All four MiniLD frontiers were blocked; the x86 failure was in replay
admission before MiniLD ran, not a linker-core failure.

The separate RV64 frozen GNU Kbuild recert is
[`41849751999`](https://github.com/yituanxing/minic-toolchain/actions/runs/41849751999):
the 2,141-object input universe was SHA authenticated and previously
QEMU-certified under GNU. MiniLD reached 2,074 linked objects and 1,279,429
relocations, then rejected `.data..ro_after_init` in `init/main.o` being
placed in `.rodata`, a legitimate GNU BFD `SHF_WRITE` union.

## Coherent compatibility and segment-layout changes

- Real Linux `.rodata` can mix `A` and `WA` `SHT_PROGBITS` members. Permit
  `SHF_WRITE` union under precise allocatable/non-executable/non-TLS
  constraints; keep illegal types and flags rejected. Compare payload,
  symbols and final section flags against GNU BFD.
- RV32 `R_RISCV_ADD64/SUB64` name their **64-bit target data field**, not
  RV64-only ISA. Preserve full 8-byte bounded arithmetic and the independent
  RV32 `R_RISCV_64` absolute-relocation prohibition.
- Honor explicit `-EL` only for targets whose ELF profile is little-endian;
  reject unsupported `-EB` or contradictory requests.
- Implement GNU `-n/--nmagic` as an explicitly selected static script mode:
  no default page-size packing, `p_align=1`, and GNU-compatible PT_LOAD
  permission union. Keep `-N/--omagic` distinct and unsupported.
- The **ordinary** scripted ET_EXEC path now checks `p_offset % page ==
  p_vaddr % page`, retains `AT()` LMA and TLS semantics, coalesces compatible
  RX/R fragments sharing a page including congruent fill holes, and rejects
  accidental RWX overlap. A NOBITS-only LOAD (`p_filesz=0`) may have a
  congruent file offset beyond EOF without referencing file bytes.
- Correct the first-link replay admission to allow genuine Kbuild
  `vmlinux.a` plus a linker script without a direct `.o` argv member.

## Recertification and boundaries

- RV64: `.github/workflows/minild-rv64-linux-cached-final-recert.yml`;
  commit marker `[minild-cached-replay]` reuses the authenticated GNU pool.
- Other four: new `.github/workflows/minild-fourarch-linux-cached-final-recert.yml`;
  marker `[minild-cached-fourarch]` restores exact saved outputs, downloads
  the matching producer's argv and GNU hashes, checks every object and
  performs one new GNU/MiniLD final-link replay per architecture.
- The five target CI boundary additionally runs GNU comparison fixtures for
  `.data..ro_after_init`, `-n`, RV32 64-bit arithmetic, explicit endian
  requests, section group/merge behavior, and scripted PT_LOAD layout.

The producer object pools are deliberately immutable and are **not rebuilt
to investigate each linker failure**. Each new MiniLD link blocker should be
diagnosed on the exact smallest input, addressed across every affected
target, then recertified against the frozen Linux inputs. Post-replay
`vmlinux`, image, and guest runtime verification remains mandatory before
calling any target complete.

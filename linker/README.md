# MiniLD linker component

**Public tool:** `minic-ld` (standalone CLI in `tools/minic-ld/`).

MiniLD is **implemented**, not an M0 placeholder. The previous README described an early
repository state and must not be used to assess current capabilities. The implementation
lives in `linker/src/`; its public C interface is `linker/include/minild.h`.

## Ownership and current target boundary

MiniLD owns input-object and archive selection, symbol resolution, relocation
application, section placement, supported linker-script interpretation, and final
ELF production. Assembly parsing belongs to MiniAS; archive production belongs
to MiniAR; preprocessing and C compilation belong to MiniPP and MiniC.

The currently exposed **library API is RISC-V ELF64-specific**:

- Relocatable links (`-r` / ET_REL): `minild_link_relocatable_elf64_riscv[_inputs]`.
- Final static links: `minild_link_static_elf64_riscv_inputs[_options]`.
- Shared-object / PIE links: `minild_link_shared_elf64_riscv_inputs[_options]`.

The input model distinguishes individual objects, archives, whole archives and
group archives. The shared-link options cover an entry point, SONAME, dynamic-list,
version-script, interpreter, PIE and selected ELF output policies. The existence
of an option or a function does **not** imply complete GNU `ld` compatibility.

The CLI has a deliberately limited `--gc-sections` capability: it is accepted
for final static linking only and rejected for `-r`, `-shared` and `-pie`.
Do not silently relax this restriction without matching correctness tests.

**Not established by this component:** a five-architecture native MiniLD, full
GNU linker-script/relocation coverage, every Linux `ld` operation, or physical
hardware qualification. MiniC's five-target C-to-assembly acceptance is a
different gate from MiniLD's native linker coverage.

## Tests and executable evidence

The source-controlled test entry points are `tests/linker/run-a0.sh` through
`run-a6.sh`, `run-script-a0.sh`, `run-script-a1.sh` and
`run-gc-v0.sh`.

Selected tests with explicit acceptance contracts:

| Test | What it checks |
| --- | --- |
| `run-a0.sh` | RV64 ET_REL output, symbols and relocation records, then GNU final-link consumption and exact `.text` comparison |
| `run-script-a1.sh` | Script-directed placement, section boundaries, output-symbol values, `PROVIDE` behavior and GNU comparisons |
| `run-a5.sh` | RV64 shared PLT/GOT/JUMP_SLOT and real QEMU `dlopen`/`dlsym` execution versus GNU |
| `run-a6.sh` | RV64 TLS dynamic relocation model, real QEMU `dlopen` and observable result versus GNU |

The workflow
[`minild-integration-v1.yml`](../.github/workflows/minild-integration-v1.yml)
also defines independently selectable `dynamic`, `linux-rel`, `static`
and `all` integration modes (manual `workflow_dispatch`).
These jobs require pinned external toolchains, runtime dependencies and
artifact provenance; merely having a workflow file does not prove that
a given commit passed it.

The **Linux REL boundary** job routes only `purgatory.ro` and `vmlinux.o`
through MiniLD and uses GNU `ld` for other links. A PASS there is not a
whole-kernel GNU-free linker qualification. Likewise, a static musl/Lua
or dynamic musl/PIE PASS is scoped to that exact run, commit and ISA.

### Local / CI reproduction

On a host with the required RV64 GNU cross-toolchain, QEMU user emulation
and other test prerequisites:

```sh
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/minild-review all
make MODE=release CFLAGS=-Werror BUILD_DIR=build/minild-review check-minild-a0
make MODE=release CFLAGS=-Werror BUILD_DIR=build/minild-review check-minild-a5
make MODE=release CFLAGS=-Werror BUILD_DIR=build/minild-review check-minild-a6
```

Run the other available `check-minild-*` targets as appropriate. Record
the exact Git SHA, CI run URL, toolchain/runtime versions, output digest
and whether the result is a native MiniLD test, a GNU-differential oracle,
a partial GNU/MiniLD mixed link or a full end-to-end link.

## Long-term maintenance / release rules

MiniLD is part of a **long-lived independent toolchain** alongside MiniPP,
MiniC, MiniAS, MiniAR and the driver. A publishable release candidate requires:

1. One exact source/commit identity for each claimed test; no historical
   or ancestor-branch result promoted to latest-head acceptance.
2. Supported inputs, flags, target ISA/ABI and incomplete GNU features
   documented as *supported*, *fail-closed* or *not yet qualified*.
3. Focused regression gates for relocations, archives, section placement,
   script expressions, dynamic linking and explicit failure/cleanup paths.
4. At least one original, unmodified real-software end-to-end workload with
   provenance and actual QEMU execution for the claimed target.
5. Separate reporting of MiniLD-native links, mixed GNU/MiniLD links, and
   the historical complete native toolchain path.

The repository README records a **historical** all-native RV64 Linux
pipeline at `24b02474` (2,064/2,064 real Linux C objects and QEMU P1).
That remains useful regression evidence, **not** a recertification of the
latest MiniC cleanup or MiniAS refactor heads.

Do not merge development branches merely to make these gates appear green.
First reconcile the dependency chain and run exact-head verification.

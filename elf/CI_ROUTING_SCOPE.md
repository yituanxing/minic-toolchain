# Shared ELF CI routing scope

This file is a deliberately non-executable, controlled **ELF-only push probe**
for the 2026-10-09 CI code-dependency audit. It changes no compiler or ELF
semantics. Its sole purpose is to produce a real GitHub `push` changing a
path under `elf/**` without changing toolchain code, tests, M0 scripts, or
workflow YAML.

The shared ELF library includes:
- `elf/src/reader.c`
- `elf/src/relocatable_writer.c`
- `elf/src/binary_export.c`
- `elf/src/rewrite.c`

An ELF-only push should start applicable shared-ELF owners, including MiniPP A0
and the MiniObjcopy/Strip T1 `regressions` job on **both** development branches.
Runtime also starts MiniC RV64, MiniAR and MiniLD focused regressions; the
Performance MiniC RV64 T1 gate is automatically eligible on its own branch.

The historical MiniObjcopy `linux-tool` and `linux-image` jobs must remain
skipped without their explicit commit tags or manual dispatch. Job SUCCESS is
not a Linux Image or QEMU boot certificate.

Compare the actual Actions run/job lists against these requirements. Do not
infer successful routing from a static M0 check alone.

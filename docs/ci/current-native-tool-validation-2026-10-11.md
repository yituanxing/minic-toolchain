# Current native tool validation — 2026-10-11

## Scope and priority

Validate only existing C-language Mini tool implementations on the runtime branch.
Do **not** restore or implement MiniObjdump, rerun historical self-hosting, or
combine native tools before isolated GNU-reference gates are established.

The reference is Linux 6.6.143 on RISC-V with GCC preprocessing/compilation,
GNU as, GNU ar, GNU ld and GNU binary utilities. In each tool's Linux proof,
replace **exactly one** producer boundary. A compiler-only MiniC campaign is
tracked separately.

## Current evidence / claims (fail closed)

- MiniC full-current campaign:
  [#38147618243](https://github.com/yituanxing/minic-toolchain/actions/runs/38147618243).
  All seven 2,064-object compile shards completed successfully; the receiver
  authenticated the complete 2,064-object MiniC pool and GNU baseline
  preflight. **Full-MiniC Linux Image link failed**, so there is no current-head
  full-MiniC Linux runtime certificate. Unresolved symbols include
  `arch_dma_set_uncached`, `dax_delete_mapping_entry` and four
  `__compiletime_assert_395..398` sites. Its prefix binary search reports
  `INTERACTION_OR_NONMONOTONE` (not a proven isolated one-object fault).
  The older **nine-current-owner + 2,055 historical owners** two-replay
  145/145 module and 203/203 function PASS remains a separate valid baseline.
- MiniPP: native preprocessing vs GCC-E differential gates exist. The
  end-to-end GCC-with-only-MiniPP Linux replacement remains unproven.
- MiniAS: frozen/real Linux assembly differential gates exist. The
  end-to-end GCC-with-only-MiniAS Linux replacement remains unproven.
- MiniAR: existing `miniar-linux-kbuild.yml` is an isolated GNU-GCC/GNU-as/
  **MiniAR**/GNU-ld Linux build and short runtime smoke. Its 90-second fast
  profile is **not** the original full 145/203 contract.
- MiniLD: existing `minild-integration-v1.yml` `linux-rel-boundaries`
  replaces GNU ld at only two selected relocatable-link outputs; other GNU ld
  calls remain. This is an **intermediate**, not a full MiniLD-only Image
  certificate. Existing static/dynamic musl tests remain separate.
- MiniNM / MiniObjcopy / MiniStrip: existing differential / ELF consumer tests
  are useful T1 correctness gates. Binary inspection and stripping are not
  equivalent to a whole-kernel boot certification.
- The unified driver has an existing independent smoke/integration gate,
  which is distinct from the compiler and full toolchain certificates.

## Reusable verification gates initiated in this campaign

One documentation-only, opt-in source-control push asks the already-active
`linux-legacy-tag-router-v1.yml` to launch the existing MiniAR Linux, MiniLD
Linux REL and MiniAS 16-real-assembly gates; the existing MiniObjcopy/Strip
regression workflow is also opted in. No new GitHub Actions workflow or compiler
source change is required.

- MiniAR Linux: `[miniar-linux-kbuild]`
- MiniLD two GNU-ld boundaries: `[minild-linux-rel-boundaries]`
- MiniAS real16 diagnostic: `[minias-real16]`
- MiniObjcopy/MiniStrip differential: `[miniobjcopy-strip-regressions]`

These are **current-head diagnostic runs**, not substitutes for an independent
single-tool, complete 145/203 Linux runtime replay. Record exact run IDs,
compiler/tool hashes, stage-specific pass/fail and genuine guest markers
before promoting a gate.

## Exit criteria before combined toolchain and new feature work

1. Each implemented tool must pass an isolated GNU-base differential and
   relevant real build/integration contract. Mark the precise supported scope.
2. For tools whose output participates in the Linux Image, demonstrate that
   exactly that tool is replaced, the other GNU tools remain pinned, and Linux
   completes the intended QEMU/functional oracle, not only a link or smoke.
3. Address remaining full-current MiniC link failures independently, preserving
   all seven independently cached 2,064-owner shards.
4. Only after independent acceptance, start two-tool integration followed by
   complete existing-toolchain integration. Postpone MiniObjdump work and
   current-compiler Stage0->Stage1->Stage2 self-host re-certification.

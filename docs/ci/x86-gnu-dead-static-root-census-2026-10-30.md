# x86 GNU Linux 6.6.143: dead static object/root census (2026-10-30)

This is a reproducible **compiler-correctness gap**, not a missing Linux
Kconfig option and not permission to synthesize unresolved Linux symbols.

## Exact GNU boundary

- CI run `41045404678`, x86 diagnostic artifact `12973357583`, original GNU
  frozen `.i` and original GNU `.o.cmd` for `drivers/md/dm.o` and
  `net/netfilter/nf_nat_proto.o`.
- Original GNU QEMU P0/P1 succeeds. The MiniC-overlaid GNU Kbuild relink fails
  with `dm_zone_endio`, `dax_get_private` (three sites),
  `dm_set_zones_restrictions`, `crc32c`, `__cfi_sites`, `__cfi_sites_end` and
  separate compile-time-assert references.
- Locally recompiling the **same frozen `.i`** using GCC 13.3 `-x cpp-output
  -O2 -mcmodel=kernel -mno-red-zone` and inspecting `nm -u` finds **none** of
  `dm_zone_endio`, `dax_get_private`, `dm_set_zones_restrictions`, `crc32c`.
  This is a differential diagnosis, not yet a byte-for-byte GNU `.o.cmd`
  replay: the original runner GCC is from a separate environment.

## Origin of each non-assertion reference

| Symbol | Frozen GNU `.i` evidence | Required compiler semantics |
| --- | --- | --- |
| `dm_zone_endio` | `clone_endio` checks `bdev_is_zoned()`. `blk_queue_zoned_model()` is `if (0) return q->limits.zoned; return BLK_ZONED_NONE;` under the frozen `CONFIG_BLK_DEV_ZONED=n`. | Inline constant propagation through helpers, then remove dead conditional call. |
| `dm_set_zones_restrictions` | `dm_table_set_restrictions()` guard depends on the same disabled zoned model. | Same, without pretending the external function exists. |
| `dax_get_private` | `dm_dax_direct_access`, `dm_dax_zero_page_range`, `dm_dax_recovery_write` are static callback functions addressed in static file-scope `dm_dax_ops`. Their only relevant registration in `alloc_dev()` is under literal `if (0)` (`CONFIG_FS_DAX=n`). | Prune unused static global object `dm_dax_ops` **and then** its now-unreferenced callback functions. |
| `crc32c` | Static `sctp_csum_update` references `crc32c` via static file-scope `sctp_csum_ops`, which is used by otherwise-dead inline `sctp_compute_cksum`. | Joint file-scope static object / static function reference-graph pruning. |
| `__cfi_sites`, `__cfi_sites_end` | Linux v6.6 `arch/x86/kernel/alternative.c`: `alternative_instructions()` passes these addresses to `__apply_fineibt(...)`, but its implementation under disabled `CONFIG_CFI_CLANG` is an empty static stub. | Safe internal empty-call inlining/removal with dead argument-address removal; NOT fake linker-script symbols. |

## Existing MiniC support and precise blind spot

`src/compiler/core_reachability.c` already walks Core `CALL`/
`FUNCTION_ADDRESS` edges, prunes dead static functions, and honors non-Core
function roots. However `minic_build_noncore_root_index()` seeds function
relocations from **every** file-scope global unconditionally. Similarly,
`src/frontend/function_body.c` seeds all file-scope global relocations and
`src/frontend/ast_global.c::minic_c0_program_global_object_emittable()`
always emits file-scope objects. These three choices create a dead global →
callback → absent external-symbol chain even after Core folds `if (0)`.

The additional C fixture
`tests/compiler/c0/gnu-dead-file-static-object-graph.c` reproduces this:
GCC `-O2` emits `retained_ops` and `.initcall6.init` but no reference to
`minic_dead_static_object_must_not_resolve`; MiniC presently emits that
undefined reference. This is an intentional failing fixture for future
correctness work, not a claimed passing regression.

## Safe implementation requirements (not yet claimed as complete)

1. Preserve `__attribute__((used))` / `__used__` at **object** semantic
   identity. Current object attribute parser treats optimization-class `used`
   as parse-only; unlike functions, global objects have no `force_emit` bit.
2. Build one joint graph over final **reachable Core blocks**, functions,
   objects, global initializers (including object/function aliases and
   relocations), source file ASM and live inline ASM symbolic references;
   only then decide which internal file-scope globals can disappear.
3. Keep GNU linker-script and assembly retained objects as roots: `used`,
   `retain`, `.initcall`, special section symbols, source ASM tokens,
   `__start`/`__stop` conventions. Ambiguous sources must fail closed by
   retaining objects, not by deleting potential roots.
4. Preserve all externally visible objects, exported aliases, explicit
   visibility, genuine address escapes and static-local function-owned
   storage. All backends must consume the same finalized emission decision.
5. Exact GNU `.i`/`.cmd` provenance, linker log `nm -u` elimination, GNU
   assembly and all ISA focused regressions are necessary before a three-ISA
   full Linux result can be claimed.

No Linux Kconfig, fake symbol definitions, or GNU object fallbacks should be
introduced to hide these compiler defects.

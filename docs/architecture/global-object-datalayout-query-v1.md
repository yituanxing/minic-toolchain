# Global-object DataLayout query ownership v1

This slice completes the object-layout ownership cleanup by making
`MinicDataLayout` the single source of derived global-object size and alignment.
Semantic `MinicGlobalObject` nodes retain object-definition inputs and policy:
type, explicit alignment, linkage/storage flags, section/visibility metadata,
initializer payload, relocations, and tentative/extern state. They no longer
cache the target-derived `storage_size` and `alignment` answers.

The new `minic_data_layout_global_object()` query owns the object-level policy
that cannot be expressed by a bare type-layout query alone. It preserves the
established handling for extern void objects, extern incomplete records,
extern incomplete arrays, and object-level explicit alignment, and otherwise
delegates size/alignment to the canonical type DataLayout.

RV64 global emission now queries this object layout wherever it needs the
object's extent or alignment. `minic_riscv64_layout_globals()` is consequently
validation-only: it verifies that every global object has a valid DataLayout
answer but writes no derived target facts into the semantic AST.

Initializer and relocation ownership is intentionally unchanged. Their payload
describes object contents and language/linkage semantics; moving size/alignment
does not imply an InitPlan design. That remains a separate frontend ownership
decision to be made after another global reread.

The Linux pressure harness retained discovery-only zero-sized-record emission.
Its helper now asks the same global-object DataLayout query whether the object
size is zero instead of reading a removed AST cache. This adapter is staging
only; the formal product slice remains independent of Linux-specific semantics.

Together with FunctionLayout and record DataLayout ownership, this sharpens the
deferred Core IR seam: semantic nodes carry language/object inputs, while all
concrete target layout answers are queried from target-owned services.

The candidate passed the official full compiler gate and frozen Linux 6.6.143
hybrid pressure run `31786550364`, which completed with
`GLOBAL_OBJECT_DATALAYOUT_HYBRID=1`, `cached_tu_status=0`, and
`FULL_TU_PASS lines=90928`.

# RV64 FunctionLayout ownership v1

This slice introduces `MinicRiscv64FunctionLayout` as the canonical RV64
owner of function-local object placement. It records the local-object offset
vector and total local storage size independently of the semantic AST.

`MinicRiscv64FrameLayout` now has a core path that consumes this FunctionLayout
result. Function emission constructs the FunctionLayout once and uses that core
path, so frame sizing no longer treats `MinicFunction.local_storage_size` as its
source of truth. The legacy three-argument frame-layout entry point remains as a
compatibility adapter for expression-level consumers and constructs a temporary
FunctionLayout internally.

This is intentionally a migration slice, not the final removal of backend facts
from the AST. `minic_riscv64_layout_program()` still mirrors the canonical local
offsets into `MinicLocal.storage_offset` and the total into
`MinicFunction.local_storage_size` because local address/load/store emitters still
consume those fields. A later bounded slice must migrate those consumers to the
FunctionLayout side state before deleting the mirror fields.

Record-field and global-object layout are deliberately outside this change. They
belong to the C object DataLayout boundary and must not be conflated with concrete
per-function stack placement.

The design preserves the deferred Core IR seam: C semantic facts and object
DataLayout remain above/along the seam, while concrete function-local placement
and frame construction stay below it in the target backend.

The candidate passed the official full compiler gate and the frozen Linux 6.6.143
hybrid pressure gate in run `31781721667`; the latter produced
`cached_tu_status=0` and `FULL_TU_PASS lines=90928`.

# RV64 local placement side-state v1

This slice completes the ownership move started by RV64 FunctionLayout v1.
Concrete local-object offsets and total local storage are no longer mirrored
into semantic AST nodes. `MinicLocal.storage_offset` and
`MinicFunction.local_storage_size` are removed.

`MinicRiscv64FunctionLayout` is now the only owner of per-function local
placement. Object address/load/store helpers resolve a `MinicLocalId` through
`minic_riscv64_function_layout_local_offset()` and range-check against the
FunctionLayout local-storage extent. `MinicRiscv64FrameLayout` consumes the
same FunctionLayout through its core API; the temporary three-argument frame
compatibility wrapper is removed.

Function emission owns one FunctionLayout for the lifetime of lowering a
function. Expression, statement, block, record-value, and inline-asm lowering
receive it explicitly as a read-only parameter. This is deliberate: the
existing backend call stack already passes `FILE *`, Program, and Function,
while the intermediate statement/inline-asm layers mostly forward data. A
codegen context would therefore broaden this ownership migration into an API
rewrite without demonstrated benefit. A context can be reconsidered later if
additional per-function backend state creates real parameter pressure.

`minic_riscv64_layout_program()` now owns only record/global object layout.
Record-field and global-object size/alignment remain part of the C object
DataLayout boundary and are intentionally not moved by this slice.

The result also sharpens the deferred Core IR seam: semantic AST nodes contain
language facts, while target-local placement and frame construction remain in
backend side state below the seam.

The canonical candidate passed the official full compiler gate and frozen
Linux 6.6.143 hybrid pressure run `31783443470`. The Linux run preserved
discovery-only aggregate/ABI semantics while injecting the same FunctionLayout
ownership and completed with `cached_tu_status=0` and
`FULL_TU_PASS lines=90928`.

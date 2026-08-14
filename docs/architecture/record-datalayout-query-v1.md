# Record DataLayout query ownership v1

This slice makes `MinicDataLayout` the single owner of derived record object
layout. Semantic record nodes retain the inputs required by the C type model:
field types and element counts, bit-field width, explicit alignment, packed
state, union/record identity, and completeness. They no longer cache target
layout outputs.

The removed derived fields are `MinicRecordField.storage_offset`,
`MinicRecordField.bit_offset`, `MinicRecord.storage_size`, and
`MinicRecord.alignment`. RV64 member addressing, bit-field lowering, record
temporaries, record copies, `offsetof` emission, and static record emission now
obtain offsets, bit offsets, size, and alignment through DataLayout queries.

`minic_riscv64_layout_records()` is consequently only a validation pass: for
complete records it verifies that DataLayout can compute the record type and
each field layout, but it does not write target-derived facts back into the AST.

This distinction is intentional. `bit_width`, `explicit_alignment`, packed
attributes, and similar properties are semantic inputs and remain in the AST;
byte/bit offsets and final size/alignment are target-derived answers and belong
to DataLayout.

Global-object `storage_size` and `alignment` are not changed here. Their
ownership includes object-level explicit alignment plus special extern/incomplete
cases, so they require a canonical global-object layout query before their cache
can be removed. Keeping that work separate avoids conflating type layout with
object-definition policy.

This also preserves the deferred Core IR seam: target-neutral record semantics
remain in the semantic model, while concrete object layout is queried from the
target DataLayout service wherever lowering needs it.

The candidate passed the official full compiler gate and frozen Linux 6.6.143
hybrid pressure run `31785059210`. That Linux run rebuilt the proven
discovery semantic/ABI/FunctionLayout environment, applied this same record
ownership patch cleanly, and completed with `cached_tu_status=0` and
`FULL_TU_PASS lines=90928`.

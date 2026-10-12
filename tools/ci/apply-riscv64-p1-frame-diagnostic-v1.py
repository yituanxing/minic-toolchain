#!/usr/bin/env python3
"""Instrumentation-only frame audit for the existing Linux Runtime MiniC profile.

Apply *after* the normal 43-piece profile is assembled. Never edit instructions,
offsets, frame sizes, object lifetime decisions, register allocation, or .s text.
A directly replayed .o MUST remain byte-identical to the certified MiniC pool.
"""
from pathlib import Path

source = Path("src/target/riscv64/core_codegen.c")
text = source.read_text()
needle = "    if (!minic_riscv64_emit_function_symbol_begin(file, symbol)) {"
if text.count(needle) != 1:
    raise SystemExit("P1_FRAME_DIAG=ERROR missing_or_ambiguous_emit_anchor")
if "P1_FRAME_DIAGNOSTIC_ONLY_V1" in text:
    raise SystemExit("P1_FRAME_DIAG=ERROR already_applied")

injection = r'''    /* P1_FRAME_DIAGNOSTIC_ONLY_V1: inspect the *final* patched profile
       layout without changing a single byte of generated assembly.
       Enable only for exact named functions via environment variables.
       value_slot_count counts 8-byte units after IRQ_FRAME_PACK_V0. */
    {
        const char *audit = getenv("MINIC_CORE_FRAME_TRACE");
        const char *filter = getenv("MINIC_CORE_FRAME_TRACE_FILTER");
        if (audit != NULL && audit[0] != '\0' &&
            (filter == NULL || filter[0] == '\0' ||
             strstr(filter, symbol_name) != NULL)) {
            size_t i;
            size_t value_bytes = frame.value_slot_count * 8U;
            size_t object_region =
                frame.value_base_offset - frame.outgoing_argument_size;
            size_t after_values =
                frame.frame_size - frame.value_base_offset - value_bytes;
            size_t total_payload = 0U;
            size_t unique_offsets = 0U;
            size_t shared_offsets = 0U;

            (void)fprintf(stderr,
                          "CORE_FRAME function=%s frame=%zu value_bytes=%zu "
                          "values=%zu objects=%zu\n",
                          symbol_name, frame.frame_size, value_bytes,
                          frame.value_count, function->object_count);
            for (i = 0U; i < function->object_count; ++i) {
                size_t element_bytes = 0U;
                size_t alignment = 0U;
                size_t j;
                bool shares_start = false;
                const char *shape = "scalar";

                if (!minic_data_layout_type(
                        minic_default_data_layout(), program,
                        function->objects[i].type,
                        &element_bytes, &alignment)) {
                    (void)fprintf(stderr,
                                  "CORE_FRAME_AUDIT_ERROR function=%s object=%zu\n",
                                  symbol_name, i);
                    continue;
                }
                if (minic_type_is_record(function->objects[i].type)) {
                    shape = "record";
                } else if (minic_type_is_array(function->objects[i].type)) {
                    shape = "array";
                }
                for (j = 0U; j < i; ++j) {
                    if (frame.object_offsets[j] == frame.object_offsets[i]) {
                        shares_start = true;
                        break;
                    }
                }
                if (shares_start) {
                    ++shared_offsets;
                } else {
                    ++unique_offsets;
                }
                if (element_bytes != 0U &&
                    function->objects[i].element_count <= SIZE_MAX / element_bytes &&
                    total_payload <=
                        SIZE_MAX - element_bytes * function->objects[i].element_count) {
                    total_payload += element_bytes * function->objects[i].element_count;
                }
                (void)fprintf(
                    stderr,
                    "CORE_FRAME_OBJECT function=%s id=%zu offset=%zu "
                    "element_bytes=%zu elements=%zu align=%zu "
                    "explicit_align=%zu kind=%s shares_start=%u\n",
                    symbol_name, i, frame.object_offsets[i], element_bytes,
                    function->objects[i].element_count, alignment,
                    function->objects[i].explicit_alignment, shape,
                    shares_start ? 1U : 0U);
                /* The mature Runtime profile installs the conservative
                   CoreScalarObjectInterval cache before frame assignment.
                   Log the *computed* block and interval, never infer them
                   from the object index or its stack offset. */
                if (core_scalar_object_reuse_cache.program == program &&
                    core_scalar_object_reuse_cache.function == function &&
                    core_scalar_object_reuse_cache.intervals != NULL) {
                    const CoreScalarObjectInterval *life =
                        &core_scalar_object_reuse_cache.intervals[i];
                    (void)fprintf(
                        stderr,
                        "CORE_FRAME_INTERVAL function=%s id=%zu reusable=%u "
                        "block=%zu first=%zu last=%zu\n",
                        symbol_name, i, life->reusable ? 1U : 0U,
                        life->block_index, life->first_position,
                        life->last_position);
                }
            }
            (void)fprintf(stderr,
                          "CORE_FRAME_COMPONENT function=%s frame=%zu "
                          "outgoing=%zu object_region=%zu value_bytes=%zu "
                          "suffix=%zu object_payload=%zu unique_offsets=%zu "
                          "shared_offsets=%zu\n",
                          symbol_name, frame.frame_size,
                          frame.outgoing_argument_size, object_region,
                          value_bytes, after_values, total_payload,
                          unique_offsets, shared_offsets);
            (void)fflush(stderr);
        }
    }
'''
text=text.replace(needle,injection+needle,1)
source.write_text(text)
print("P1_FRAME_DIAG=APPLIED instrumentation_only=true")

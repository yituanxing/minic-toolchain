from pathlib import Path

root = Path('.')
p = root / 'src/target/riscv64/codegen_function.c'
text = p.read_text()

old = r'''static bool minic_riscv64_emit_integer_bits(FILE *file, size_t width, uint64_t bits) {
    const char *directive;
    uint64_t mask;
    int64_t signed_value;

    directive = minic_riscv64_integer_data_directive(width);
    if (file == NULL || directive == NULL) {
        return false;
    }
    if (width == 1U) {
        return fprintf(file, "  %s %u\n", directive, (unsigned int)(bits & UINT64_C(0xff))) >= 0;
    }
    if (width < 8U) {
        unsigned int bit_width;
        uint64_t sign_bit;

        bit_width = (unsigned int)(width * 8U);
        mask = (UINT64_C(1) << bit_width) - UINT64_C(1);
        bits &= mask;
        sign_bit = UINT64_C(1) << (bit_width - 1U);
        if ((bits & sign_bit) != 0U) {
            bits |= ~mask;
        }
    }
    (void)memcpy(&signed_value, &bits, sizeof(signed_value));
    return fprintf(file, "  %s %" PRId64 "\n", directive, signed_value) >= 0;
}
'''
new = r'''static bool minic_riscv64_emit_typed_bits(FILE *file,
                                           const MinicC0Program *program,
                                           MinicType type,
                                           uint64_t bits) {
    const char *directive;
    size_t width;
    size_t alignment;
    uint64_t mask;

    if (file == NULL || program == NULL ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
        !minic_riscv64_type_layout(program, type, &width, &alignment) ||
        (width != 1U && width != 2U && width != 4U && width != 8U)) {
        return false;
    }
    (void)alignment;
    directive = minic_riscv64_integer_data_directive(width);
    if (directive == NULL) {
        return false;
    }
    if (width < 8U) {
        const unsigned int bit_width = (unsigned int)(width * 8U);

        mask = (UINT64_C(1) << bit_width) - UINT64_C(1);
        bits &= mask;
    }
    /* Preserve the historical byte spelling as an unsigned payload. GNU as
     * consumes the same low 8 bits, and plain char on this target is unsigned. */
    if (width == 1U || (minic_type_is_integer(type) && minic_type_is_unsigned_integer(type))) {
        return fprintf(file, "  %s %" PRIu64 "\n", directive, bits) >= 0;
    }
    {
        int64_t signed_value;

        if (width < 8U) {
            const unsigned int bit_width = (unsigned int)(width * 8U);
            const uint64_t sign_bit = UINT64_C(1) << (bit_width - 1U);

            if ((bits & sign_bit) != 0U) {
                bits |= ~((UINT64_C(1) << bit_width) - UINT64_C(1));
            }
        }
        (void)memcpy(&signed_value, &bits, sizeof(signed_value));
        return fprintf(file, "  %s %" PRId64 "\n", directive, signed_value) >= 0;
    }
}
'''
if text.count(old) != 1:
    raise SystemExit(f'typed bits helper anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)

replacements = [
    (
        '''            if (!minic_riscv64_emit_integer_bits(file, field_size, value)) {\n''',
        '''            if (!minic_riscv64_emit_typed_bits(file, program, field->type, value)) {\n''',
        'direct record typed bits'),
    (
        '''        } else if (!minic_riscv64_emit_integer_bits(file, type_size, bits)) {\n''',
        '''        } else if (!minic_riscv64_emit_typed_bits(file, program, type, bits)) {\n''',
        'recursive aggregate typed bits'),
    (
        '''            if (directive == NULL || !minic_riscv64_emit_integer_bits(file, field_size, value)) {\n''',
        '''            if (directive == NULL ||\n                !minic_riscv64_emit_typed_bits(file, program, field->type, value)) {\n''',
        'record array typed bits'),
    (
        '''            if (!minic_riscv64_emit_integer_bits(\n                    file, scalar_width, object->initializer_values[initializer_index])) {\n''',
        '''            if (!minic_riscv64_emit_typed_bits(\n                    file, program, scalar_type, object->initializer_values[initializer_index])) {\n''',
        'global scalar typed bits'),
]
for old_call, new_call, label in replacements:
    count = text.count(old_call)
    if count != 1:
        raise SystemExit(f'{label} anchor mismatch: {count}')
    text = text.replace(old_call, new_call, 1)

p.write_text(text)

# The aggregate focused gate should now assert the semantic unsigned spelling,
# while the pointer sentinel intentionally remains signed textual -1.
p = root / 'tests/compiler/c0/run-static-aggregate-initializers.sh'
text = p.read_text()
old = "grep -F '.word -559067475' \"$build_dir/static_record_compound_literal.s\" >/dev/null\n"
new = "grep -F '.word 3735899821' \"$build_dir/static_record_compound_literal.s\" >/dev/null\n"
if text.count(old) != 1:
    raise SystemExit(f'aggregate unsigned spelling anchor mismatch: {text.count(old)}')
p.write_text(text.replace(old, new, 1))

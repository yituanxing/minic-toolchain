#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_ir.c")
source = path.read_text()

# Instrument verify_block. A POINTER_OFFSET failure gets a compact SSA/CFG dump
# locating the base/index definition blocks and all CFG edges in the function.
old_instruction = """        if (!instruction_is_valid(function, instruction, available_values)) {
            return false;
        }
"""
new_instruction = """        if (instruction->kind == MINIC_CORE_INSTRUCTION_POINTER_OFFSET) {
            MinicCoreValueId diag_base = instruction->value.pointer_offset.base;
            MinicCoreValueId diag_index = instruction->value.pointer_offset.index;
            bool diag_result_valid = instruction_result_is_valid(function, instruction);
            bool diag_base_in_range = diag_base < function->value_count;
            bool diag_index_in_range = diag_index < function->value_count;
            bool diag_base_available = diag_base_in_range && available_values[diag_base];
            bool diag_index_available = diag_index_in_range && available_values[diag_index];
            bool diag_base_type_equal =
                diag_base_in_range && minic_type_equal(function->values[diag_base].type,
                                                        instruction->type);
            bool diag_index_integer =
                diag_index_in_range && minic_type_is_integer(function->values[diag_index].type);
            (void)fprintf(stderr,
                          "M126A_POINTER_OFFSET_DIAG block=%" PRIu32 " slot=%zu instruction=%" PRIu32
                          " result=%" PRIu32 " base=%" PRIu32 " index=%" PRIu32
                          " stride=%zu subtract=%d result_valid=%d type_ptr=%d"
                          " base_range=%d base_avail=%d base_type_equal=%d base_def=%" PRIu32
                          " index_range=%d index_avail=%d index_integer=%d index_def=%" PRIu32
                          " span=%zu:%zu-%zu:%zu\\n",
                          block_id,
                          index,
                          instruction_id,
                          instruction->result,
                          diag_base,
                          diag_index,
                          instruction->value.pointer_offset.element_size,
                          instruction->value.pointer_offset.subtract ? 1 : 0,
                          diag_result_valid ? 1 : 0,
                          minic_type_is_pointer(instruction->type) ? 1 : 0,
                          diag_base_in_range ? 1 : 0,
                          diag_base_available ? 1 : 0,
                          diag_base_type_equal ? 1 : 0,
                          diag_base_in_range ? function->values[diag_base].definition
                                             : MINIC_CORE_INSTRUCTION_INVALID,
                          diag_index_in_range ? 1 : 0,
                          diag_index_available ? 1 : 0,
                          diag_index_integer ? 1 : 0,
                          diag_index_in_range ? function->values[diag_index].definition
                                              : MINIC_CORE_INSTRUCTION_INVALID,
                          instruction->span.begin.line,
                          instruction->span.begin.column,
                          instruction->span.end.line,
                          instruction->span.end.column);
            if (!diag_index_available || !diag_base_available) {
                MinicCoreInstructionId diag_base_def =
                    diag_base_in_range ? function->values[diag_base].definition
                                       : MINIC_CORE_INSTRUCTION_INVALID;
                MinicCoreInstructionId diag_index_def =
                    diag_index_in_range ? function->values[diag_index].definition
                                        : MINIC_CORE_INSTRUCTION_INVALID;
                size_t diag_scan_block;
                for (diag_scan_block = 0U; diag_scan_block < function->block_count;
                     ++diag_scan_block) {
                    const MinicCoreBlock *diag_block = &function->blocks[diag_scan_block];
                    size_t diag_scan_slot;
                    for (diag_scan_slot = 0U; diag_scan_slot < diag_block->instruction_count;
                         ++diag_scan_slot) {
                        MinicCoreInstructionId diag_id = diag_block->instructions[diag_scan_slot];
                        if (diag_id == diag_base_def || diag_id == diag_index_def) {
                            const MinicCoreInstruction *diag_def = &function->instructions[diag_id];
                            (void)fprintf(stderr,
                                          "M126A_DEF_LOC value=%" PRIu32 " instruction=%" PRIu32
                                          " block=%zu slot=%zu kind=%d result=%" PRIu32
                                          " span=%zu:%zu-%zu:%zu\\n",
                                          diag_id == diag_base_def ? diag_base : diag_index,
                                          diag_id,
                                          diag_scan_block,
                                          diag_scan_slot,
                                          (int)diag_def->kind,
                                          diag_def->result,
                                          diag_def->span.begin.line,
                                          diag_def->span.begin.column,
                                          diag_def->span.end.line,
                                          diag_def->span.end.column);
                        }
                    }
                    if (diag_block->has_terminator) {
                        const MinicCoreTerminator *diag_term = &diag_block->terminator;
                        if (diag_term->kind == MINIC_CORE_TERMINATOR_BRANCH) {
                            (void)fprintf(stderr,
                                          "M126A_CFG_EDGE from=%zu kind=branch to=%" PRIu32 "\\n",
                                          diag_scan_block,
                                          diag_term->branch_target);
                        } else if (diag_term->kind == MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH) {
                            (void)fprintf(stderr,
                                          "M126A_CFG_EDGE from=%zu kind=cond condition=%" PRIu32
                                          " true=%" PRIu32 " false=%" PRIu32 "\\n",
                                          diag_scan_block,
                                          diag_term->conditional.condition,
                                          diag_term->conditional.when_true,
                                          diag_term->conditional.when_false);
                        } else {
                            (void)fprintf(stderr,
                                          "M126A_CFG_EDGE from=%zu kind=%d terminal=1\\n",
                                          diag_scan_block,
                                          (int)diag_term->kind);
                        }
                    } else {
                        (void)fprintf(stderr,
                                      "M126A_CFG_EDGE from=%zu kind=none terminal=0\\n",
                                      diag_scan_block);
                    }
                }
            }
        }
        if (!instruction_is_valid(function, instruction, available_values)) {
            (void)fprintf(stderr,
                          "M126A_VERIFY_BLOCK_FAIL block=%" PRIu32 " slot=%zu instruction=%" PRIu32 " kind=%d result=%" PRIu32 " values=%zu asms=%zu\\n",
                          block_id,
                          index,
                          instruction_id,
                          (int)instruction->kind,
                          instruction->result,
                          function->value_count,
                          function->inline_asm_count);
            return false;
        }
"""
if source.count(old_instruction) != 1:
    raise SystemExit("verify_block instruction anchor mismatch")
source = source.replace(old_instruction, new_instruction, 1)

old_terminator = """    return terminator_is_valid(function, &block->terminator, available_values);
}

bool minic_core_function_verify"""
new_terminator = """    if (!terminator_is_valid(function, &block->terminator, available_values)) {
        (void)fprintf(stderr,
                      "M126A_VERIFY_TERMINATOR_FAIL block=%" PRIu32 " kind=%d instructions=%zu\\n",
                      block_id,
                      (int)block->terminator.kind,
                      block->instruction_count);
        return false;
    }
    return true;
}

bool minic_core_function_verify"""
if source.count(old_terminator) != 1:
    raise SystemExit("verify_block terminator anchor mismatch")
source = source.replace(old_terminator, new_terminator, 1)

start_marker = "bool minic_core_function_verify(const MinicCoreFunction *function) {"
end_marker = "bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {"
begin = source.find(start_marker)
if begin < 0:
    raise SystemExit("Core verifier start marker not found")
end = source.find(end_marker, begin)
if end < 0:
    raise SystemExit("Core verifier dump marker not found")
body = source[begin:end]
needle = "return false;"
count = body.count(needle)
if count == 0:
    raise SystemExit("Core verifier has no false returns to instrument")
replacement = (
    'do { '
    '(void)fprintf(stderr, "M126A_VERIFY_DIRECT_FAIL line=%d function=%s\\n", '
    '__LINE__, function != NULL && function->name != NULL ? function->name : "?"); '
    'return false; '
    '} while (0);'
)
body = body.replace(needle, replacement)
old_final = """    return valid;
}

"""
new_final = """    if (!valid) {
        (void)fprintf(stderr,
                      "M126A_VERIFY_FINAL_FALSE function=%s blocks=%zu instructions=%zu values=%zu asms=%zu\\n",
                      function->name,
                      function->block_count,
                      function->instruction_count,
                      function->value_count,
                      function->inline_asm_count);
    }
    return valid;
}

"""
if body.count(old_final) != 1:
    raise SystemExit("Core verifier final return anchor mismatch")
body = body.replace(old_final, new_final, 1)
path.write_text(source[:begin] + body + source[end:])
print(f"M126A verifier diagnostic staged direct_false_returns={count}")

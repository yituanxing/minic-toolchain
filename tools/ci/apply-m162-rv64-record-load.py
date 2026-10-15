#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M162 materializer {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


replace_once(
    '''        !minic_data_layout_type(
            minic_default_data_layout(), program, instruction->type, &size, &alignment) ||
        (size != 1U && size != 2U && size != 4U && size != 8U)) {
''',
    '''        !minic_data_layout_type(
            minic_default_data_layout(), program, instruction->type, &size, &alignment) ||
        size == 0U) {
''',
    "record-load-capability",
)

old_emit = '''    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const char *opcode;
        size_t destination_offset;
        size_t record_size;

        if (!core_record_load_supported(program, function, instruction, &record_size) ||
            !core_object_offset(program,
                                function,
                                instruction->value.record_load.destination_object,
                                &destination_offset) ||
            !load_core_value(
                file, frame, instruction->value.record_load.source_address, "t0")) {
            return false;
        }
        opcode = record_size == 8U ? "ld" : record_size == 4U ? "lwu" :
                 record_size == 2U ? "lhu" : "lbu";
        if (fprintf(file, "  %s t1, 0(t0)\\n", opcode) < 0 ||
            !emit_sp_store_chunk(file, "t1", destination_offset, record_size)) {
            return false;
        }
        return true;
    }
'''
new_emit = '''    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const char *opcode;
        size_t destination_offset;
        size_t record_size;

        if (!core_record_load_supported(program, function, instruction, &record_size) ||
            !core_object_offset(program,
                                function,
                                instruction->value.record_load.destination_object,
                                &destination_offset) ||
            !load_core_value(
                file, frame, instruction->value.record_load.source_address, "t0")) {
            return false;
        }
        if (record_size <= 8U) {
            opcode = record_size == 8U ? "ld" : record_size == 4U ? "lwu" :
                     record_size == 2U ? "lhu" : "lbu";
            if (fprintf(file, "  %s t1, 0(t0)\\n", opcode) < 0 ||
                !emit_sp_store_chunk(file, "t1", destination_offset, record_size)) {
                return false;
            }
            return true;
        }

        /* M162_CORE_RV64_RECORD_LOAD: materialize arbitrary non-empty records
           into the destination CoreObject without assuming source alignment.
           This mirrors RECORD_COPY's byte-safe O0 fallback. */
        if (!emit_sp_address(file, "t1", destination_offset)) {
            return false;
        }
        {
            size_t copied = 0U;
            while (copied < record_size) {
                size_t chunk = record_size - copied;
                size_t offset;
                if (chunk > 2048U) {
                    chunk = 2048U;
                }
                for (offset = 0U; offset < chunk; ++offset) {
                    if (fprintf(file,
                                "  lbu t2, %zu(t0)\\n"
                                "  sb t2, %zu(t1)\\n",
                                offset,
                                offset) < 0) {
                        return false;
                    }
                }
                copied += chunk;
                if (copied < record_size &&
                    fprintf(file,
                            "  li t3, %zu\\n"
                            "  add t0, t0, t3\\n"
                            "  add t1, t1, t3\\n",
                            chunk) < 0) {
                    return false;
                }
            }
        }
        return true;
    }
'''
replace_once(old_emit, new_emit, "record-load-emitter")

PATH.write_text(text)
print("M162_RECORD_LOAD_APPLIED")

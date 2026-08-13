from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()
old = r'''    while (addressed != NULL && addressed->kind == MINIC_EXPRESSION_MEMBER) {
        if (depth == MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
            return false;
        }
        reverse_path[depth] = addressed->value.member.field_index;
        depth += 1U;
        addressed = minic_c0_program_expression(program, addressed->value.member.base);
    }
    if (addressed == NULL || addressed->kind != MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return false;
    }
'''
new = r'''    while (addressed != NULL && addressed->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;

        if (depth == MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
            return false;
        }
        reverse_path[depth] = addressed->value.member.field_index;
        depth += 1U;
        base = minic_c0_program_expression(program, addressed->value.member.base);
        if (base != NULL && base->kind == MINIC_EXPRESSION_ADDRESS_OF) {
            addressed = minic_c0_program_expression(program, base->value.unary.operand);
        } else {
            addressed = base;
        }
    }
    if (addressed == NULL || addressed->kind != MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f'step4 direct-member path anchor mismatch: {text.count(old)}')
p.write_text(text.replace(old, new, 1))

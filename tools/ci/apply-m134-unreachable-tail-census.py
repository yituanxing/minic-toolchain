#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M134_UNREACHABLE_TAIL_CENSUS'
if marker in text:
    print('M134 unreachable-tail census already staged')
    raise SystemExit(0)

old = '''            if (statement->kind != MINIC_STATEMENT_LABEL) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
new = '''            if (statement->kind != MINIC_STATEMENT_LABEL) {
                /* M134_UNREACHABLE_TAIL_CENSUS: diagnose the previously silent
                   fail-closed edge after a CFG-terminating statement. */
                (void)fprintf(stderr,
                              "CORE_M134_UNREACHABLE_TAIL function=%s kind=%d "
                              "span=%zu:%zu cleanup=%llu stop=%llu\\n",
                              context->source_function->name,
                              (int)statement->kind,
                              statement->span.begin.line,
                              statement->span.begin.column,
                              (unsigned long long)statement->cleanup_context,
                              (unsigned long long)statement->cleanup_stop_context);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
'''
if text.count(old) != 1:
    raise SystemExit(f'expected exactly one unreachable-tail guard, found {text.count(old)}')
path.write_text(text.replace(old, new, 1))
print('M134 unreachable-tail census staged')

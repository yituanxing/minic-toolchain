#!/usr/bin/env python3
from pathlib import Path
import re

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M139_LOCAL_LOOP_PASSTHROUGH_TRACE'
if marker in text:
    print('M139 local/loop passthrough trace already staged')
    raise SystemExit(0)

# 1) Trace lower_local_object entry and every concrete exit for __readahead_batch.
start_anchor = 'static MinicCoreLowerStatus lower_local_object(MinicCoreLowerContext *context,'
end_anchor = '\nstatic MinicCoreLowerStatus lower_parameter_ingress'
start = text.find(start_anchor)
end = text.find(end_anchor, start)
if start < 0 or end < 0:
    raise SystemExit('M139 lower_local_object region changed')
region = text[start:end]
brace = region.find('{')
if brace < 0:
    raise SystemExit('M139 lower_local_object opening brace missing')
entry = r'''
    /* M139_LOCAL_LOOP_PASSTHROUGH_TRACE: diagnostics only. */
    if (getenv("CORE_M139_TRACE") != NULL && context != NULL &&
        context->source_function != NULL && context->source_function->name != NULL &&
        strcmp(context->source_function->name, "__readahead_batch") == 0) {
        const MinicLocal *m139_local =
            (context->body != NULL && context->body->program != NULL)
                ? minic_c0_program_local(context->body->program, local_id)
                : NULL;
        (void)fprintf(stderr,
                      "CORE_M139_LOCAL_ENTER function=%s local=%llu begin=%llu count=%zu local_objects=%d found=%d type_base=%d array=%d elem_count=%zu register=%d\n",
                      context->source_function->name,
                      (unsigned long long)local_id,
                      (unsigned long long)context->source_function->local_begin,
                      context->source_function->local_count,
                      context->local_objects != NULL ? 1 : 0,
                      m139_local != NULL ? 1 : 0,
                      m139_local != NULL ? (int)m139_local->type.base : -1,
                      m139_local != NULL && m139_local->is_array ? 1 : 0,
                      m139_local != NULL ? m139_local->element_count : 0U,
                      m139_local != NULL && m139_local->is_register_storage ? 1 : 0);
    }
'''
region = region[:brace+1] + entry + region[brace+1:]
site = 0
pat = re.compile(r'(?P<indent>^[ \t]*)return (?P<expr>MINIC_CORE_LOWER_(?:OK|ERROR|UNSUPPORTED)|status);', re.M)

def trace_local_return(m):
    global site
    site += 1
    indent = m.group('indent')
    expr = m.group('expr')
    return (f'{indent}if (getenv("CORE_M139_TRACE") != NULL && context != NULL &&\n'
            f'{indent}    context->source_function != NULL && context->source_function->name != NULL &&\n'
            f'{indent}    strcmp(context->source_function->name, "__readahead_batch") == 0) {{\n'
            f'{indent}    (void)fprintf(stderr, "CORE_M139_LOCAL_RETURN function=%s local=%llu site={site} expr={expr}\\n",\n'
            f'{indent}                  context->source_function->name, (unsigned long long)local_id);\n'
            f'{indent}}}\n'
            f'{indent}return {expr};')

region, local_return_count = pat.subn(trace_local_return, region)
text = text[:start] + region + text[end:]

# 2) Trace lower_while passthrough returns (`return status;`) for wait_task_inactive.
start_anchor = 'static MinicCoreLowerStatus\nlower_while(MinicCoreLowerContext *context,'
end_anchor = '\nstatic bool core_inline_asm_constraint_is'
start = text.find(start_anchor)
end = text.find(end_anchor, start)
if start < 0 or end < 0:
    raise SystemExit('M139 lower_while region changed')
region = text[start:end]
pass_site = 0
status_pat = re.compile(r'(?P<indent>^[ \t]*)return status;', re.M)

def trace_status_return(m):
    global pass_site
    pass_site += 1
    indent = m.group('indent')
    return (f'{indent}if (getenv("CORE_M139_TRACE") != NULL && context != NULL &&\n'
            f'{indent}    context->source_function != NULL && context->source_function->name != NULL &&\n'
            f'{indent}    strcmp(context->source_function->name, "wait_task_inactive") == 0) {{\n'
            f'{indent}    (void)fprintf(stderr, "CORE_M139_WHILE_PASSTHROUGH function=%s site={pass_site} status=%d span=%zu:%zu\\n",\n'
            f'{indent}                  context->source_function->name, (int)status,\n'
            f'{indent}                  statement->span.begin.line, statement->span.begin.column);\n'
            f'{indent}}}\n'
            f'{indent}return status;')

region, pass_count = status_pat.subn(trace_status_return, region)
if pass_count == 0:
    raise SystemExit('M139 found no lower_while passthrough returns')
text = text[:start] + region + text[end:]
path.write_text(text)
print(f'M139 local/loop passthrough trace staged local_returns={local_return_count} while_passthrough={pass_count}')

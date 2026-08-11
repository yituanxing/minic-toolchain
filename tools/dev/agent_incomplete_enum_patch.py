from pathlib import Path

root = Path(__file__).resolve().parents[2]

# Parser-owned enum tags gain a bounded incomplete/complete lifecycle.
internal = root / "src/frontend/parser_internal.h"
text = internal.read_text()
old = '''typedef struct MinicParserEnumTag {
    MinicSourceSpan name_span;
} MinicParserEnumTag;
'''
new = '''typedef struct MinicParserEnumTag {
    MinicSourceSpan name_span;
    bool is_complete;
} MinicParserEnumTag;
'''
if text.count(old) != 1:
    raise SystemExit("enum tag state anchor mismatch")
internal.write_text(text.replace(old, new, 1))

# Replace the enum-tag registry operations, keeping enumerator handling unchanged.
enum_path = root / "src/frontend/parser_enum.c"
text = enum_path.read_text()
start = text.find("bool minic_parser_find_enum_tag(")
end = text.find("bool minic_parser_find_enum_constant(", start)
if start < 0 or end < 0:
    raise SystemExit("enum registry boundaries missing")
registry = r'''static MinicParserEnumTag *find_enum_tag_binding(MinicParser *parser,
                                                   MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return NULL;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return &parser->enum_tags[index - 1U];
        }
    }
    return NULL;
}

static bool append_enum_tag(MinicParser *parser, MinicSourceSpan name_span, bool is_complete) {
    MinicParserEnumTag *resized;
    size_t new_capacity;

    if (parser == NULL) {
        return false;
    }
    if (parser->enum_tag_count == parser->enum_tag_capacity) {
        new_capacity = parser->enum_tag_capacity == 0U ? 8U : parser->enum_tag_capacity * 2U;
        if (new_capacity < parser->enum_tag_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_tags)) {
            minic_parser_error(parser, "too many enum tags");
            return false;
        }
        resized = (MinicParserEnumTag *)realloc(parser->enum_tags,
                                                new_capacity * sizeof(*parser->enum_tags));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum tag");
            return false;
        }
        parser->enum_tags = resized;
        parser->enum_tag_capacity = new_capacity;
    }
    parser->enum_tags[parser->enum_tag_count].name_span = name_span;
    parser->enum_tags[parser->enum_tag_count].is_complete = is_complete;
    parser->enum_tag_count += 1U;
    return true;
}

bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return false;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return true;
        }
    }
    return false;
}

static bool declare_incomplete_enum_tag(MinicParser *parser, MinicSourceSpan name_span) {
    if (find_enum_tag_binding(parser, name_span) != NULL) {
        return true;
    }
    return append_enum_tag(parser, name_span, false);
}

bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span) {
    MinicParserEnumTag *tag;

    if (parser == NULL) {
        return false;
    }
    tag = find_enum_tag_binding(parser, name_span);
    if (tag != NULL) {
        if (tag->is_complete) {
            minic_parser_error(parser, "duplicate enum definition");
            return false;
        }
        tag->is_complete = true;
        return true;
    }
    return append_enum_tag(parser, name_span, true);
}

'''
enum_path.write_text(text[:start] + registry + text[end:])

# Unknown tagged references now create an incomplete tag; a later definition completes it.
text = enum_path.read_text()
old = '''    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (!has_tag || !minic_parser_find_enum_tag(parser, tag_span)) {
            minic_parser_error(parser,
                               has_tag ? "unknown enum tag" : "expected enum tag or definition");
            return false;
        }
        *enum_type = minic_type_int();
        return true;
    }
'''
new = '''    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (!has_tag) {
            minic_parser_error(parser, "expected enum tag or definition");
            return false;
        }
        if (!declare_incomplete_enum_tag(parser, tag_span)) {
            return false;
        }
        *enum_type = minic_type_int();
        return true;
    }
'''
if text.count(old) != 1:
    raise SystemExit("enum reference anchor mismatch")
enum_path.write_text(text.replace(old, new, 1))

# Translation-unit dispatch recognizes `enum tag;` as a standalone declaration,
# just like an enum definition, instead of forcing it through object/function parsing.
function = root / "src/frontend/parser_function.c"
text = function.read_text()
start = text.find("static bool enum_keyword_starts_definition(")
end = text.find("static bool record_keyword_starts_standalone_declaration(", start)
if start < 0 or end < 0:
    raise SystemExit("enum dispatch helper boundaries missing")
helper = r'''static bool enum_keyword_starts_standalone_declaration(MinicParser *parser,
                                                       bool *is_standalone) {
    MinicParser probe;

    if (parser == NULL || is_standalone == NULL || parser->current.kind != MINIC_TOKEN_KW_ENUM) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected enum tag or definition after enum keyword");
        return false;
    }
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_standalone =
        probe.current.kind == MINIC_TOKEN_SEMICOLON || probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

'''
text = text[:start] + helper + text[end:]
old = '''        } else if (parser.current.kind == MINIC_TOKEN_KW_ENUM) {
            bool is_definition;

            if (!enum_keyword_starts_definition(&parser, &is_definition)) {
                success = false;
            } else if (is_definition) {
                success = minic_parser_parse_enum_definition(&parser);
            } else {
                success = parse_function(&parser, false);
            }
'''
new = '''        } else if (parser.current.kind == MINIC_TOKEN_KW_ENUM) {
            bool is_standalone;

            if (!enum_keyword_starts_standalone_declaration(&parser, &is_standalone)) {
                success = false;
            } else if (is_standalone) {
                success = minic_parser_parse_enum_definition(&parser);
            } else {
                success = parse_function(&parser, false);
            }
'''
if text.count(old) != 1:
    raise SystemExit("enum top-level dispatch anchor mismatch")
function.write_text(text.replace(old, new, 1))

# Evolve the existing enum tag contract around the real Linux forward-reference shapes.
fixture = root / "tests/compiler/c0/enum_tag_type_references.c"
fixture.write_text(r'''struct hrtimer;
struct fwnode_handle;

enum lockdep_like {
    LOCKDEP_LIKE_OK,
    LOCKDEP_LIKE_BAD,
};

extern enum system_states_like {
    SYSTEM_BOOTING_LIKE,
    SYSTEM_RUNNING_LIKE,
} system_state_like;

typedef enum system_states_like system_state_alias;

/* Linux timer.h shape: first use is a function return type, definition later. */
extern enum hrtimer_restart it_real_fn(struct hrtimer *);

/* Linux trace shape: first use is a function-pointer typedef return type. */
typedef enum print_line_t (*trace_print_func)(void);

/* Linux security.h shape: explicit standalone incomplete enum declaration. */
enum fs_value_type;

/* Linux fwnode.h shape: first use is a function-pointer record field return type. */
struct fwnode_operations_like {
    enum dev_dma_attr (*device_get_dma_attr)(const struct fwnode_handle *fwnode);
};

enum hrtimer_restart {
    HRTIMER_NORESTART,
    HRTIMER_RESTART,
};

enum print_line_t {
    TRACE_TYPE_PARTIAL_LINE,
    TRACE_TYPE_HANDLED,
};

enum fs_value_type {
    FS_VALUE_UNDEFINED,
    FS_VALUE_FLAG,
};

enum dev_dma_attr {
    DEV_DMA_NOT_SUPPORTED,
    DEV_DMA_NON_COHERENT,
};

extern void add_taint_like(unsigned flag, enum lockdep_like state);
enum lockdep_like report_bug_like(unsigned long address, enum lockdep_like state);

static enum lockdep_like normalize_state(enum lockdep_like state) {
    return state;
}

static enum hrtimer_restart timer_result(void) {
    return HRTIMER_RESTART;
}

static enum fs_value_type fs_value(void) {
    return FS_VALUE_FLAG;
}

int main(void) {
    system_state_alias state = SYSTEM_BOOTING_LIKE;
    trace_print_func printer = (trace_print_func)0;
    struct fwnode_operations_like ops = {0};

    return normalize_state(LOCKDEP_LIKE_OK) + state + timer_result() + fs_value() +
           (printer != 0) + (ops.device_get_dma_attr != 0);
}
''')

runner = root / "tests/compiler/c0/run-enum-tag-type-references.sh"
runner.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-tag-type-references
assembly="$work/enum_tag_type_references.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_tag_type_references.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'normalize_state:' "$assembly" >/dev/null
grep -F 'timer_result:' "$assembly" >/dev/null
grep -F 'fs_value:' "$assembly" >/dev/null

cat >"$work/duplicate.c" <<'EOF'
enum duplicate_tag;
enum duplicate_tag { DUPLICATE_A };
enum duplicate_tag { DUPLICATE_B };
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/duplicate.c" -o "$work/duplicate.i"
if "$minic" -S "$work/duplicate.i" -o "$work/duplicate.s" 2>"$work/duplicate.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/enum_tag_type_references: duplicate completed enum accepted' >&2
    exit 1
fi
grep -F 'duplicate enum definition' "$work/duplicate.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/enum_tag_type_references lifecycle=incomplete-to-complete implicit-return=1 function-pointer-typedef=1 explicit-forward=1 record-function-pointer=1 representation=int duplicate-definition=reject'
''')
runner.chmod(0o755)

# Make the temporary lowering limitation visible instead of pretending full enum identity exists.
deviations = root / "docs/DEVIATIONS.md"
text = deviations.read_text()
anchor = "## Resolved deviations / 已解决偏离\n"
entry = r'''## DEV-0004: incomplete enum tags before first-class enum types / 一等 enum 类型建立前的不完整 enum tag

- Status / 状态: Active
- Rule / 规则: Accepted GNU C syntax should preserve semantic distinctions needed for later correctness; temporary lowering shortcuts must remain explicit and bounded. 已接受的 GNU C 语法应保留后续正确性需要的语义区别；临时 lowering 简化必须显式且范围受控。
- Scope / 范围: `src/frontend/parser_enum.c`, parser-owned enum tag lifecycle, current `MinicType` integer representation, and Linux 6.6.143 incomplete-enum references. 涉及 enum tag 生命周期、当前 `MinicType` 的整数表示，以及 Linux 6.6.143 的不完整 enum 引用。
- Reason / 原因: Linux requires GNU C incomplete enum tags before their later definitions, including function return types, function-pointer typedefs/fields, and explicit `enum tag;` declarations. MiniC currently lowers every enum semantic type to `int`; introducing a first-class enum TypeKind/TypeId in this blocker would also require integer promotions, compatibility, ConstEval, DataLayout, ABI, typedef and diagnostic migration. Linux 需要在后续定义前引用 GNU C incomplete enum tag，包括函数返回类型、函数指针 typedef/字段和显式 `enum tag;` 声明。MiniC 当前把 enum semantic type 统一 lowering 为 `int`；若在本 blocker 中立即引入一等 enum TypeKind/TypeId，会同时牵动整数提升、兼容性、ConstEval、DataLayout、ABI、typedef 与诊断迁移。
- Risk / 风险: The parser now preserves incomplete/complete tag lifecycle, but the resulting semantic `MinicType` does not retain enum identity or incompleteness. Therefore full enforcement of rules that depend on incomplete enum object size/storage, enum-to-enum type identity, or implementation-defined underlying type is deferred. Parser 会保存 incomplete/complete tag 生命周期，但生成的 semantic `MinicType` 尚不保留 enum identity/incompleteness，因此依赖 incomplete enum 对象尺寸/存储、enum 间类型身份或 implementation-defined underlying type 的完整规则仍被延迟。
- Exit criteria / 退出条件:
  1. Add program-owned canonical enum entities/IDs and make `MinicType` preserve enum identity through typedefs, pointers, functions and AST storage. 增加 Program-owned canonical enum entity/ID，并让 `MinicType` 在 typedef、pointer、function 与 AST 存储中保留 enum identity。
  2. Make completeness and object-storage validation query the canonical enum entity rather than parser-local state. 让 completeness/object-storage 校验查询 canonical enum entity，而非 parser-local 状态。
  3. Route enum integer representation, promotion, compatibility, DataLayout/ABI and ConstEval through the target/type model, then add negative tests for storage of incomplete enum objects and differential tests for completed enums. 将 enum 整数表示、提升、兼容性、DataLayout/ABI 与 ConstEval 接入 target/type model，并补充 incomplete enum 对象存储负例与 completed enum 差分测试。
- Target milestone / 目标里程碑: TypeContext / first-class enum identity before claiming complete GNU enum semantics / 宣称完整 GNU enum 语义前的 TypeContext / 一等 enum identity 阶段。
- Related evidence / 相关证据: Linux 6.6.143 `init/main.i` first references `enum hrtimer_restart` at line 14970 and defines it at line 23808; the same TU also contains forward-use shapes for `dev_dma_attr`, `fs_value_type`, and `print_line_t`. Linux 6.6.143 `init/main.i` 在 14970 首次引用 `enum hrtimer_restart`，到 23808 才定义；同一 TU 还包含 `dev_dma_attr`、`fs_value_type`、`print_line_t` 的前向使用形状。

'''
if text.count(anchor) != 1:
    raise SystemExit("deviation insertion anchor mismatch")
deviations.write_text(text.replace(anchor, entry + anchor, 1))

print("PASS generated incomplete enum tag lifecycle slice")

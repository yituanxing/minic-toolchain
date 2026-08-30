#!/usr/bin/env sh
set -eu

MINIPP=${MINIPP:?MINIPP is required}
REFERENCE_CPP=${REFERENCE_CPP:-gcc}
BUILD_DIR=${BUILD_DIR:-build}
work="$BUILD_DIR/tests/preprocessor/a0"

rm -rf "$work"
mkdir -p "$work"

reference_version="$("$REFERENCE_CPP" --version | head -n 1)"
printf 'MINIPP_REFERENCE_CPP=%s\n' "$reference_version"

run_exact() {
    name=$1
    shift
    "$REFERENCE_CPP" -E -P -undef -nostdinc -x c "$@"         "$work/$name.c" -o "$work/$name.gcc.i"
    "$MINIPP" -E -P -undef -nostdinc -x c "$@"         "$work/$name.c" -o "$work/$name.mini.i"
    if ! cmp -s "$work/$name.gcc.i" "$work/$name.mini.i"; then
        printf 'MINIPP_A0_EXACT=FAIL case=%s\n' "$name" >&2
        diff -u "$work/$name.gcc.i" "$work/$name.mini.i" >&2 || true
        return 1
    fi
    printf 'MINIPP_A0_CASE=PASS case=%s\n' "$name"
}

cat >"$work/plain.c" <<'EOF'
int plain = 7;
EOF
run_exact plain

cat >"$work/object.c" <<'EOF'
#define ANSWER 42
int answer = ANSWER;
EOF
run_exact object

cat >"$work/nested.c" <<'EOF'
#define A B
#define B 9
int nested = A;
EOF
run_exact nested

cat >"$work/conditional.c" <<'EOF'
#define ENABLED 1
#if ENABLED
int selected = ENABLED;
#else
int selected = 0;
#endif
EOF
run_exact conditional

cat >"$work/ifdef.c" <<'EOF'
#define PRESENT 1
#ifdef PRESENT
int yes = 1;
#endif
EOF
run_exact ifdef

cat >"$work/cli.c" <<'EOF'
int cli = VALUE;
EOF
run_exact cli -DVALUE=11

cat >"$work/comment.c" <<'EOF'
int/* comment */value = 3;
EOF
run_exact comment

cat >"$work/local.h" <<'EOF'
#define LOCAL_VALUE 17
int from_local_header = LOCAL_VALUE;
EOF
cat >"$work/quoted-include.c" <<'EOF'
#include "local.h"
int after_local = LOCAL_VALUE + 1;
EOF
run_exact quoted-include

mkdir -p "$work/inc"
cat >"$work/inc/angle.h" <<'EOF'
#define ANGLE_VALUE 23
int from_angle_header = ANGLE_VALUE;
EOF
cat >"$work/angle-include.c" <<'EOF'
#include <angle.h>
int after_angle = ANGLE_VALUE + 1;
EOF
run_exact angle-include -I"$work/inc"

mkdir -p "$work/nested"
cat >"$work/nested/leaf.h" <<'EOF'
#define LEAF_VALUE 31
EOF
cat >"$work/nested/root.h" <<'EOF'
#include "leaf.h"
int from_nested_header = LEAF_VALUE;
EOF
cat >"$work/nested-include.c" <<'EOF'
#include "nested/root.h"
int after_nested = LEAF_VALUE + 1;
EOF
run_exact nested-include

cat >"$work/function.c" <<'EOF'
#define ADD(a, b) ((a)+(b))
int function_value = ADD(2, 3);
EOF
run_exact function

cat >"$work/prescan.c" <<'EOF'
#define VALUE 7
#define ID(x) x
int prescan_value = ID(VALUE);
EOF
run_exact prescan

cat >"$work/function-nested.c" <<'EOF'
#define TWICE(x) ((x)+(x))
#define WRAP(x) TWICE(x)
int nested_function_value = WRAP(4);
EOF
run_exact function-nested

cat >"$work/function-zero.c" <<'EOF'
#define ZERO() 0
int zero_value = ZERO();
EOF
run_exact function-zero

cat >"$work/empty-macro-padding.c" <<'EOF'
#define EMPTY_ATTR(x)
int empty_macro_padding(void)
{
       EMPTY_ATTR(lock)
        EMPTY_ATTR(lock);
}
EOF
run_exact empty-macro-padding

mkdir -p "$work/forced"
cat >"$work/forced/forced.h" <<'EOF'
#define FORCED_VALUE 37
int from_forced_header = FORCED_VALUE;
EOF
cat >"$work/forced-include.c" <<'EOF'
int after_forced = FORCED_VALUE + 1;
EOF
run_exact forced-include -I"$work/forced" -include forced.h

mkdir -p "$work/system"
cat >"$work/system/system-header.h" <<'EOF'
#define SYSTEM_VALUE 41
int from_system_header = SYSTEM_VALUE;
EOF
cat >"$work/system-include.c" <<'EOF'
#include <system-header.h>
int after_system = SYSTEM_VALUE + 1;
EOF
run_exact system-include -isystem "$work/system"

cat >"$work/splice-source.c" <<'EOF'
int splice_source = 1 + \
2;
EOF
run_exact splice-source

cat >"$work/splice-macro.c" <<'EOF'
#define SPLICE_ADD(a, b) ((a)+\
(b))
int splice_macro = SPLICE_ADD(3, 4);
EOF
run_exact splice-macro

cat >"$work/variadic.c" <<'EOF'
#define PICK(first, second, ...) second
int variadic_pick = PICK(1, 2, 3, 4);
EOF
run_exact variadic

cat >"$work/token-paste.c" <<'EOF'
#define CAT(a, b) a##b
#define token42 99
int token_paste_value = CAT(token, 42);
EOF
run_exact token-paste

cat >"$work/stringize.c" <<'EOF'
#define STR(x) #x
const char *stringized = STR(hello world);
EOF
run_exact stringize

cat >"$work/stringize-forward-padding.c" <<'EOF'
#define SINK(a, b) FINAL(a, b)
#define DIRECT(x) FINAL("k", #x)
#define FORWARD(x) SINK("k", #x)
#define VPASS(fmt, ...) VSINK(fmt, ##__VA_ARGS__)
#define VSINK(fmt, ...) FINAL(fmt, ##__VA_ARGS__)
#define VFORWARD(x) VPASS("k", #x)
#define NAMED_PACK(args...) VPASS(args)
#define NAMED_FORWARD(x) NAMED_PACK("k", #x)
#define BR_PRINT(fmt, ...) FINAL(fmt, ##__VA_ARGS__)
#define BR_CONT(fmt, ...) BR_PRINT(fmt, ##__VA_ARGS__)
#define BR_SEQ(m, args...) do { if (m) seq(m, args); else BR_CONT(args); } while (0)
#define BR_STRINGIZE(x) BR_SEQ(m, "fmt", #x, value)
#define BR_MID(s, v) BR_SEQ(m, "fmt", s, v)
#define BR_STRINGIZE_MID(x) BR_MID(#x, value)
DIRECT(IRQ_LEVEL)
FORWARD(IRQ_LEVEL)
VFORWARD(IRQ_LEVEL)
NAMED_FORWARD(IRQ_LEVEL)
BR_STRINGIZE(IRQ_LEVEL)
BR_STRINGIZE_MID(IRQ_LEVEL)
EOF
run_exact stringize-forward-padding

cat >"$work/nested-stringize-origin.c" <<'EOF'
#define NS_INNER(ns) .ascii #ns "\\0"
#define NS_STRINGIFY_1(x) #x
#define NS_STRINGIFY(x) NS_STRINGIFY_1(x)
NS_STRINGIFY(NS_INNER(CXL))
EOF
run_exact nested-stringize-origin

cat >"$work/variadic-prescan-boundary-padding.c" <<'EOF'
#define ZERO_ARG(x) 0
#define SPLIT_ARG(x) hi(ZERO_ARG(x)), lo(ZERO_ARG(x))
#define VP_OUT(fmt, ...) FINAL(fmt, ##__VA_ARGS__)
#define VP_CONT(fmt, ...) VP_OUT(fmt, ##__VA_ARGS__)
#define VP_SEQ(m, args...) do { if (m) direct(m, args); else VP_CONT(args); } while (0)
VP_SEQ(m, "fmt", SPLIT_ARG(value))
EOF
run_exact variadic-prescan-boundary-padding

cat >"$work/fixed-parameter-boundary-padding.c" <<'EOF'
#define FP_OUT(fmt, ...) FINAL(fmt, ##__VA_ARGS__)
#define FP_CONT(fmt, ...) FP_OUT(fmt, ##__VA_ARGS__)
#define FP_SEQ(m, args...) do { if (m) direct(m, args); else FP_CONT(args); } while (0)
#define FP_BRIDGE(s, v) FP_SEQ(m, "fmt", s, v)
FP_BRIDGE("name", value)
EOF
run_exact fixed-parameter-boundary-padding

cat >"$work/gnu-to-bare-variadic-source-spacing.c" <<'EOF'
#define NR_FUNCTION(function, ...) function(__VA_ARGS__)
#define NR_WARN(fmt, ...) NR_FUNCTION(NR_PRINT, fmt, ##__VA_ARGS__)
#define NR_PRINT(fmt, ...) FINAL(fmt, ##__VA_ARGS__)
NR_WARN("fmt",
        dev_name, queue_index)
EOF
run_exact gnu-to-bare-variadic-source-spacing

cat >"$work/gnu-double-forward-first-vararg.c" <<'EOF'
#define GF_EMIT(_fmt) ((void)0)
#define GF_INDEX(_func, _fmt, ...) ({ GF_EMIT(_fmt); _func(_fmt, ##__VA_ARGS__); })
#define GF_PRINT(fmt, ...) GF_INDEX(GF_FINAL, fmt, ##__VA_ARGS__)
#define GF_OUTER(cond) do { if (cond) { GF_PRINT("Assertion %s %s", first_arg, second_arg); } } while (0)
GF_PRINT("Top %s %s",
         first_arg, second_arg)
GF_OUTER(1)
EOF
run_exact gnu-double-forward-first-vararg

cat >"$work/kconfig-style.c" <<'EOF'
#define __ARG_PLACEHOLDER_1 0,
#define __take_second_arg(__ignored, val, ...) val
#define __is_defined(x) ___is_defined(x)
#define ___is_defined(val) ____is_defined(__ARG_PLACEHOLDER_##val)
#define ____is_defined(arg1_or_junk) __take_second_arg(arg1_or_junk 1, 0)
#define CONFIG_MINIPP_FEATURE 1
int kconfig_yes = __is_defined(CONFIG_MINIPP_FEATURE);
int kconfig_no = __is_defined(CONFIG_MINIPP_MISSING);
EOF
run_exact kconfig-style

cat >"$work/if-relational.c" <<'EOF'
#if __STDC_VERSION__ < 202311L
int pre_c23 = 1;
#else
int pre_c23 = 0;
#endif
EOF
run_exact if-relational -D__STDC_VERSION__=201710L

cat >"$work/if-precedence.c" <<'EOF'
#define LEFT 3
#define RIGHT 4
#if defined(LEFT) && !defined(MISSING) && ((LEFT + RIGHT * 2) == 11)
int precedence = 1;
#else
int precedence = 0;
#endif
EOF
run_exact if-precedence

cat >"$work/if-bitwise-ternary.c" <<'EOF'
#define MASK 0x6U
#if ((MASK & 0x2U) != 0U) ? ((8 >> 1) == 4) : 0
int bitwise_ternary = 1;
#else
int bitwise_ternary = 0;
#endif
EOF
run_exact if-bitwise-ternary

cat >"$work/multiline-invocation.c" <<'EOF'
#define SUM3(a, b, c) ((a) + (b) + (c))
int multiline_invocation = SUM3(1,
                                2,
                                3);
EOF
run_exact multiline-invocation

cat >"$work/pragma.c" <<'EOF'
  #   pragma   GCC   diagnostic   push
#pragma GCC diagnostic ignored "-Wunused-variable"
int pragma_value;
#pragma GCC diagnostic pop
EOF
run_exact pragma

cat >"$work/pragma-operator.c" <<'EOF'
#define DO_PRAGMA(x) _Pragma(#x)
#define DIAG_STR1(x) #x
#define DIAG_STR(x) DIAG_STR1(x)
#define DIAG(x) _Pragma(DIAG_STR(GCC diagnostic x))
#define DIAG_PUSH() DIAG(push)
#define DIAG_POP() DIAG(pop)
DO_PRAGMA(GCC diagnostic push);
DO_PRAGMA(GCC diagnostic ignored "-Wmissing-prototypes");
DIAG_PUSH();
int pragma_operator_value;
DIAG_POP();
DO_PRAGMA(GCC diagnostic pop);
EOF
run_exact pragma-operator

cat >"$work/object-to-function-rescan.c" <<'EOF'
#define TARGET(x) ((x) + 1)
#define ALIAS TARGET
#define CHAIN ALIAS
int alias_call = ALIAS(4);
int chain_call = CHAIN(5);
EOF
run_exact object-to-function-rescan

cat >"$work/computed-include.h" <<'EOF'
#define COMPUTED_INCLUDE_VALUE 53
EOF
cat >"$work/computed-include.c" <<'EOF'
#define STR1(x) #x
#define STR(x) STR1(x)
#define HEADER_NAME computed-include.h
#define HEADER(x) STR(x)
#include HEADER(HEADER_NAME)
int computed_include = COMPUTED_INCLUDE_VALUE;
EOF
run_exact computed-include

cat >"$work/gnu-empty-variadic.c" <<'EOF'
#define STATIC_ASSERT(expr, ...) INNER_ASSERT(expr, ##__VA_ARGS__, #expr)
#define INNER_ASSERT(expr, msg, ...) _Static_assert(expr, msg)
STATIC_ASSERT(1 + 1 == 2);
EOF
run_exact gnu-empty-variadic

cat >"$work/builtin-location.c" <<'EOF'
const char *builtin_file = __FILE__;
int builtin_line = __LINE__;
#define BUILTIN_LINE() __LINE__
int macro_builtin_line = BUILTIN_LINE();
EOF
run_exact builtin-location

cat >"$work/builtin-header.h" <<'EOF'
const char *header_builtin_file = __FILE__;
int header_builtin_line = __LINE__;
EOF
cat >"$work/builtin-include.c" <<'EOF'
#include "builtin-header.h"
int after_builtin_header = __LINE__;
EOF
run_exact builtin-include

cat >"$work/builtin-counter.c" <<'EOF'
#define COUNTER_PASTE2(a, b) a##b
#define COUNTER_PASTE(a, b) COUNTER_PASTE2(a, b)
int counter_zero = __COUNTER__;
int counter_one = __COUNTER__;
int COUNTER_PASTE(unique_, __COUNTER__) = 2;
EOF
run_exact builtin-counter

cat >"$work/multiline-builtin-line.c" <<'EOF'
#define WHERE(x) __LINE__
int multiline_builtin_line = WHERE(
    1
);
EOF
run_exact multiline-builtin-line

cat >"$work/unused-counter-arg.c" <<'EOF'
#define SECOND(a, b) b
int counter_before = __COUNTER__;
int counter_macro = SECOND(
    __COUNTER__,
    __COUNTER__);
int counter_after = __COUNTER__;
EOF
run_exact unused-counter-arg

cat >"$work/directive-in-macro-arg.c" <<'EOF'
#define IDENTITY(x) x
int directive_in_arg = IDENTITY(
#if 1
    7
#else
    9
#endif
);
EOF
run_exact directive-in-macro-arg

cat >"$work/whitespace-render.c" <<'EOF'
		int leading_tabs;
int		interior_tabs	=	1;


int after_blank_lines;
/* comment-only line */
#define EMPTY_LINE_MACRO()
EMPTY_LINE_MACRO()
int after_empty_macro;
EOF
run_exact whitespace-render

cat >"$work/pasted-function-boundary.c" <<'EOF'
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define TARGET_0(x) ((x) + 1)
#define DISPATCH(flag, x) CAT(TARGET_, flag)(x)
int pasted_function_boundary = DISPATCH(0, 4);
EOF
run_exact pasted-function-boundary

cat >"$work/gnu-variadic-spacing.c" <<'EOF'
#define VARGS(...) f(__VA_ARGS__)
#define GNU_VARGS(first, ...) f(first, ## __VA_ARGS__)
int vargs_tight = VARGS(1,2);
int vargs_spaced = VARGS(1, 2);
int gnu_one = GNU_VARGS(1, 2);
int gnu_tight = GNU_VARGS(1,2);
int gnu_many = GNU_VARGS(1, 2, 3);
EOF
run_exact gnu-variadic-spacing

cat >"$work/nested-argument-line.c" <<'EOF'
#define INNER_LINE() __LINE__
#define PASS_LINE(x) x
int nested_argument_line = PASS_LINE(
    INNER_LINE()
);
EOF
run_exact nested-argument-line

cat >"$work/nested-mid-argument-line.c" <<'EOF'
#define INNER_MID_LINE() __LINE__
#define PASS_MID_LINE(x) x
int nested_mid_argument_line = PASS_MID_LINE(1 +
    INNER_MID_LINE());
EOF
run_exact nested-mid-argument-line

cat >"$work/gnu-variadic-invocation-line.c" <<'EOF'
#define LINE_WRAP(fmt, ...) LINE_SINK(fmt, ##__VA_ARGS__)
#define LINE_SINK(fmt, ...) FINAL(fmt, ##__VA_ARGS__)
LINE_WRAP("line",
          __LINE__)
EOF
run_exact gnu-variadic-invocation-line

cat >"$work/nested-variadic-padding.c" <<'EOF'
#define STMT(x) ({ x; })
#define WRAP_IF(cond, func, ...) ({ if (cond) func(__VA_ARGS__); })
#define WRAP(func, ...) WRAP_IF(1, func, ##__VA_ARGS__)
#define IDX(fn, fmt, ...) ({ fn(fmt, ##__VA_ARGS__); })
#define PRINT(fmt, ...) IDX(F, fmt, ##__VA_ARGS__)
#define ONCE(fmt, ...) WRAP(PRINT, fmt, ##__VA_ARGS__)
#define INFO(fmt, ...) ONCE("x" fmt, ##__VA_ARGS__)
#define PASTE_ONLY(fmt, ...) PRINT(fmt, ##__VA_ARGS__)
IDX(F, "a", STMT(1))
PRINT("b", STMT(2))
WRAP(PRINT, "c", STMT(3))
ONCE("d", STMT(4))
INFO("e", STMT(5))
PASTE_ONLY("p", STMT(6))
EOF
run_exact nested-variadic-padding

cat >"$work/named-variadic-prescan-spacing.c" <<'EOF'
#define NAMED_OBJ CUR()
#define NAMED_FORWARD(format...) NAMED_TARGET(format)
#define NAMED_TARGET(...) F(__VA_ARGS__)
NAMED_FORWARD("x", NAMED_OBJ, NAMED_OBJ)
EOF
run_exact named-variadic-prescan-spacing

cat >"$work/pp-number-padding.c" <<'EOF'
#define MINUS_ONE(x) x-1
#define PLUS_ONE(x) x+1
#define DOT_ZERO(x) x.0
int minus_number = MINUS_ONE(64);
int plus_number = PLUS_ONE(0x10U);
int dot_number = DOT_ZERO(1);
int minus_ident = MINUS_ONE(name);
EOF
run_exact pp-number-padding

cat >"$work/object-number-padding.c" <<'EOF'
#define BITS_PER_WORD 64
#define HEX_LIMIT 0x10U
int object_minus = BITS_PER_WORD-1;
int object_plus = HEX_LIMIT+1;
int object_dot = BITS_PER_WORD.0;
EOF
run_exact object-number-padding

cat >"$work/leading-comment-columns.c" <<'EOF'
	/**/TOKEN_A
	/*x*/TOKEN_B
	/*open
close*/TOKEN_C
EOF
run_exact leading-comment-columns

cat >"$work/empty-leading-nested-macro.c" <<'EOF'
#define EMPTY(x)
#define KEEP(x) x
#define OUT(x) EMPTY(x) KEEP(x)
#define OUT4(x) EMPTY(x)    KEEP(x)
OUT(column_zero)
		OUT(indented)
OUT4(column_zero_four)
		OUT4(indented_four)
EOF
run_exact empty-leading-nested-macro

printf 'MINIPP_A0_EXACT=PASS cases=57 mode=byte-identical\n'

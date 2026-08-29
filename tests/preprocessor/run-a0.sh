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

printf 'MINIPP_A0_EXACT=PASS cases=33 mode=byte-identical\n'

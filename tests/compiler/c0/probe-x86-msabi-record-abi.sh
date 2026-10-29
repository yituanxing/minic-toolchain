#!/usr/bin/env bash
# GNU Linux EFI function-pointer ABI is part of the TYPE, not a diagnostic
# attribute. Parsing declarations must not accidentally authorize SysV CALL.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf '%s\n' "X86_MSABI_FIELD=FAIL $*" >&2; exit 1; }
reject() {
    local target=$1 name=$2 expected=$3
    if "$minic" --target="$target" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.out" 2>"$work/$name.err"; then
        fail "unsafe source admitted target=$target case=$name"
    fi
    test ! -e "$work/$name.s" || fail "partial assembly left for $name"
    grep -Eq "$expected" "$work/$name.err" || {
        sed -n '1,12p' "$work/$name.err" >&2
        fail "missing fail-closed ABI diagnostic for $name"
    }
}
preprocess() {
    "$host_cc" -E -P -std=gnu11 -x c "$work/$1.c" -o "$work/$1.i"
}
test -x "$minic" || fail 'MiniC compiler missing'

# Two function pointer fields share the same machine-size record slot but
# DIFFER in ABI type identity. An unrelated default SysV declaration of the
# same callback is not retroactively converted to MS x64.
cat >"$work/declarations.c" <<'SRC'
typedef unsigned long efi_status_t;
typedef efi_status_t efi_get_time_t(unsigned long, void *);
struct EfiServices {
    efi_get_time_t __attribute__((ms_abi)) *get_time;
    efi_get_time_t *sysv_time;
};
struct EFI32 { efi_get_time_t __attribute__((ms_abi)) *get_time; };
extern struct EfiServices firmware;
unsigned long table_bytes(void) { return sizeof(struct EfiServices); }
unsigned long plain_bytes(void) { return sizeof(struct EFI32); }
unsigned long get_sysv_slot(void) { return sizeof(firmware.sysv_time); }
SRC
preprocess declarations
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -c "$work/declarations.c" \
    -o "$work/declarations-gnu.o" || fail 'GNU rejected reference fixture'
"$minic" --target=x86_64-linux -S "$work/declarations.i" \
    -o "$work/declarations.s" || fail 'MiniC rejected well-typed EFI table'
test -s "$work/declarations.s" || fail 'missing positive assembly'
"$host_cc" -c "$work/declarations.s" -o "$work/declarations-minic.o" \
    || fail 'GNU AS rejected target assembly'

# A parameter may put __attribute__((ms_abi)) BEFORE the pointer `*`, as in
# Linux EFI's efi_call_acpi_prm_handler declaration. Preserve its identity.
cat >"$work/parameter.c" <<'SRC'
typedef unsigned long efi_status_t;
efi_status_t efi_call_handler(
    efi_status_t (__attribute__((ms_abi)) *handler)(unsigned long, void *),
    unsigned long address, void *context);
unsigned long handler_size(void) { return sizeof(void *); }
SRC
preprocess parameter
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -c "$work/parameter.c" \
    -o "$work/parameter-gnu.o" || fail 'GNU rejected parenthesized form'
"$minic" --target=x86_64-linux -S "$work/parameter.i" \
    -o "$work/parameter.s" || fail 'MiniC rejected parenthesized form'

# A supported fixed-scalar MS ABI function-pointer CALL now has a genuine
# Win64 register/stack plan (including the mandatory 32-byte home area).
cat >"$work/call.c" <<'SRC'
typedef long efi_callback_t(long, long);
struct Table { efi_callback_t __attribute__((ms_abi)) *callback; };
long ms_scalar_indirect_call(struct Table *tab) { return tab->callback(11, 22); }
SRC
preprocess call
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -c "$work/call.c" \
    -o "$work/call-gnu.o" || fail 'GNU reference does not accept call form'
"$minic" --target=x86_64-linux -S "$work/call.i" -o "$work/call.s" ||
    fail 'MiniC rejected supported fixed-scalar indirect MS ABI call'
grep -F 'subq $32, %rsp' "$work/call.s" >/dev/null ||
    fail 'MS ABI indirect call omitted 32-byte shadow space'
grep -F 'call *%r11' "$work/call.s" >/dev/null ||
    fail 'MS ABI indirect call omitted target register'

cat >"$work/direct-declaration.c" <<'SRC'
extern __attribute__((ms_abi)) long efi_direct(long address);
long call_efi_direct(void) { return efi_direct(7); }
SRC
preprocess direct-declaration
"$minic" --target=x86_64-linux -S "$work/direct-declaration.i" \
    -o "$work/direct-declaration.s" ||
    fail 'MiniC rejected supported MS ABI direct call declaration'
grep -F 'subq $32, %rsp' "$work/direct-declaration.s" >/dev/null ||
    fail 'MS ABI direct call omitted 32-byte shadow space'

# MS ABI function *definitions* still require Win64 incoming parameter,
# nonvolatile-save and return ABI lowering; they remain fail-closed.
cat >"$work/direct.c" <<'SRC'
__attribute__((ms_abi)) long efi_direct(long address) { return address + 1; }
long call_efi_direct(void) { return efi_direct(7); }
SRC
preprocess direct
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -c "$work/direct.c" \
    -o "$work/direct-gnu.o" || fail 'GNU rejected direct MS ABI source'
reject x86_64-linux direct 'ms_abi function definition requires Microsoft x64 incoming ABI'

# A pure type declaration using the x86-only ABI attribute may not be
# treated as a portable pointer on LP64D/AAPCS64 (where it is meaningless).
preprocess declarations
for target in aarch64-linux loongarch64-linux; do
    if "$minic" --target="$target" -S "$work/declarations.i" \
        -o "$work/foreign-${target}.s" >"$work/foreign.out" 2>"$work/foreign.err"; then
        fail "ms_abi silently admitted by foreign target: $target"
    fi
    test ! -e "$work/foreign-${target}.s" || fail 'foreign partial output'
done

printf '%s\n' 'X86_MSABI_FIELD=PASS GNU_decl=2 MiniC_GNU_AS=2 typed_table=1 parenthesized_parameter=1 direct_call=1 indirect_call=1 definition_reject=1 foreign_target_reject=2'

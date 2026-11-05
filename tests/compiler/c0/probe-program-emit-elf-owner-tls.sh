#!/bin/sh
# Exercise the actual Common ELF writer and independent Core LP64 direct
# emitters. GNU AS/readelf, rather than text matching alone, proves binding.
set -eu

probe_dir=$(mktemp -d)
trap 'rm -rf -- "$probe_dir"' EXIT HUP INT TERM
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
cd "$repo_root"

host_cc=${HOST_CC:-cc}
"$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Iinclude -Isrc tests/compiler/c0/probe-program-emit-elf-owner-tls.c \
  src/core/core_ir.c src/core/core_type.c src/core/module_ir.c \
  src/core/value_uses.c src/frontend/ast_global.c \
  src/target/common/core_function_facts.c \
  src/target/common/core_function_plan.c \
  src/target/common/program_visit.c \
  src/target/common/program_emit.c \
  src/target/common/direct_program_emit.c \
  src/target/common/elf_module_symbol_info.c \
  src/target/common/module_global_emit.c \
  src/target/backend_core_usage.c \
  src/compiler/global_payload.c src/target/common/global_emit.c \
  -o "$probe_dir/owner-tls"

"$probe_dir/owner-tls" "$probe_dir/owner.s" "$probe_dir/tls.s"
"$host_cc" -x assembler -c "$probe_dir/owner.s" -o "$probe_dir/owner.o"
readelf -Ws "$probe_dir/owner.o" > "$probe_dir/symbols.txt"

check_binding() {
  symbol=$1
  expected=$2
  actual=$(awk -v name="$symbol" '$NF == name { print $5 }' "$probe_dir/symbols.txt")
  if [ "$actual" != "$expected" ]; then
    echo "ELF_SYMBOL_OWNER=FAIL symbol=$symbol actual=$actual expected=$expected" >&2
    exit 1
  fi
}

# Weak declarations exist both before and after their actual owners.
for strong in weak_before weak_after weak_fn weak_after_fn \
              cross_object_owner cross_function_owner; do
  check_binding "$strong" GLOBAL
  if grep -Fq ".weak $strong" "$probe_dir/owner.s"; then
    echo "ELF_SYMBOL_OWNER=FAIL spurious_weak=$strong" >&2
    exit 1
  fi
done
check_binding missing_object WEAK
check_binding missing_fn WEAK
if grep -Eq 'unused_object|unused_fn' "$probe_dir/owner.s"; then
  echo 'ELF_SYMBOL_OWNER=FAIL unused_weak_decl_emitted' >&2
  exit 1
fi

echo 'MINIC_REAL_GNU_ELF_OWNER=PASS strong=6 cross_kind=2 undefined_weak=2 unused=2 GNU_AS=PASS readelf=PASS'

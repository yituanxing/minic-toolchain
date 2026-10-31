#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo_root"
cc=${CC:-cc}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

common_flags=(-std=c11 -Wall -Wextra -Werror -Iassembler/src)
"$cc" "${common_flags[@]}" \
    tests/assembler/mc-operand-lifetime.c \
    assembler/src/mc_operand_semantics.c -o "$scratch/lifetime"
"$scratch/lifetime"

# AddressSanitizer checks nested union ownership and error-path cleanup when
# supported.  LeakSanitizer requires /proc task access unavailable under some
# sandbox ptrace profiles; keep that separate from memory-access validation.
sanitizer=unsupported
if "$cc" "${common_flags[@]}" -fsanitize=address \
       -fno-omit-frame-pointer \
       tests/assembler/mc-operand-lifetime.c \
       assembler/src/mc_operand_semantics.c \
       -o "$scratch/lifetime-asan" 2>/dev/null; then
    ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
        "$scratch/lifetime-asan"
    sanitizer=address-only-PASS
fi

printf 'MINIAS_MC_OPERAND_LIFETIME=PASS typed_heap_graph=1 partial_parse=1 legacy_fallback=1 asan=%s\n' "$sanitizer"

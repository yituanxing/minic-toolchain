#!/usr/bin/env bash
# Keep the proven-aligned RV I payload fast path and unknown/packed bytewise
# path distinct. The same test covers 64-bit float payloads on RV32I and RV64I.
set -Eeuo pipefail
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source_file="$root/tests/compiler/c0/riscv_aligned_payload_memory.c"
cc -E -P -x c "$source_file" -o "$work/source.i"

for arch in riscv32 riscv64; do
    if [[ "$arch" == riscv32 ]]; then
        isa=rv32i
        abi=ilp32
        wide='(lw|sw)'
    else
        isa=rv64i
        abi=lp64
        wide='(ld|sd)'
    fi
    "$MINIC_CC" --target="$arch-linux" -march="$isa" -mabi="$abi" \
        -S "$work/source.i" -o "$work/$arch.s"
    if grep -Eq '^  (flw|fsw|fld|fsd) ' "$work/$arch.s"; then
        echo "RV_ALIGNED_PAYLOAD=FAIL target=$arch unexpected_FD=1" >&2
        exit 1
    fi
    awk -v wide="$wide" -v arch="$arch" '
      /^(aligned_local_payload|unknown_pointer_payload|packed_field_payload):$/ {
          function_name = substr($0, 1, length($0) - 1)
          next
      }
      /^\.size (aligned_local_payload|unknown_pointer_payload|packed_field_payload),/ {
          function_name = ""
      }
      function_name == "aligned_local_payload" {
          if ($0 ~ "^  " wide " x6, [0-9]+\\(x5\\)$") native++
          if ($0 ~ /^  (lbu|sb) /) aligned_byte++
      }
      function_name == "unknown_pointer_payload" && /^  lbu / { unknown_byte++ }
      function_name == "packed_field_payload" && /^  lbu / { packed_byte++ }
      END {
          if (native < 4 || aligned_byte != 0 ||
              unknown_byte < 8 || packed_byte < 8) {
              printf "RV_ALIGNED_PAYLOAD=FAIL target=%s native=%d aligned_byte=%d unknown_byte=%d packed_byte=%d\n", \
                  arch, native, aligned_byte, unknown_byte, packed_byte > "/dev/stderr"
              exit 1
          }
          printf "RV_ALIGNED_PAYLOAD=PASS target=%s native=%d aligned_byte=%d unknown_byte=%d packed_byte=%d\n", \
              arch, native, aligned_byte, unknown_byte, packed_byte
      }
    ' "$work/$arch.s"
done

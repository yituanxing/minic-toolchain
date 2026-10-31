#!/usr/bin/env bash
# A retained RVC branch must be able to grow through *two* MC alternatives:
# C.BNEZ (2 bytes) -> BNE (4 bytes) -> BEQ + JAL (8 bytes).  In particular,
# the Common forced-fragment bitmap must not suppress the second growth.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=${WORK_DIR:-"$(mktemp -d)"}
mkdir -p "$work"

for case_name in compressed branch32 branch_long source_replay_long; do
  case "$case_name" in
    compressed) padding=100; width=2 ;;
    branch32) padding=320; width=4 ;;
    branch_long|source_replay_long) padding=5000; width=8 ;;
  esac
  if [[ $case_name == source_replay_long ]]; then
    printf '.text\n.option rvc\n.globl entry\nentry:\n  bne s0, zero, target\n.set branch_span, . - entry\n.if . - .\n.error "unreachable"\n.endif\n.space %s\ntarget:\n  ret\n.data\n.long branch_span\n' \
      "$padding" >"$work/$case_name.s"
  else
    printf '.text\n.option rvc\n.globl entry\nentry:\n  bne s0, zero, target\n  .space %s\ntarget:\n  ret\n' \
      "$padding" >"$work/$case_name.s"
  fi
  if [[ $case_name == source_replay_long ]]; then
    MINIAS_TRACE_RELAYOUT=1 "$minias" --target=riscv -march=rv64gc -mabi=lp64d \
      -o "$work/$case_name.o" "$work/$case_name.s" 2>"$work/$case_name.trace"
    grep -qx 'minias-source-reparse:pass=1' "$work/$case_name.trace"
    grep -qx 'minias-source-reparse:pass=2' "$work/$case_name.trace"
    if grep -q '^minias-source-reparse:pass=3$' "$work/$case_name.trace"; then
      echo 'unexpected third SourceGraph replay' >&2
      exit 1
    fi
  else
    "$minias" --target=riscv -march=rv64gc -mabi=lp64d \
      -o "$work/$case_name.o" "$work/$case_name.s"
  fi
  python3 - "$work/$case_name.o" "$width" "$padding" "$case_name" <<'PY'
import struct
import sys

filename, expected_width, padding, case_name = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
data = open(filename, 'rb').read()
assert data[:4] == b'\x7fELF' and data[4] == 2 and data[5] == 1
shoff = struct.unpack_from('<Q', data, 40)[0]
entsize, count, shstrndx = struct.unpack_from('<HHH', data, 58)
headers = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * entsize)
           for i in range(count)]
strs = headers[shstrndx]
names = data[strs[4]:strs[4] + strs[5]]
sections = {}
for entry in headers:
    offset = entry[0]
    name = names[offset:names.find(b'\0', offset)].decode('ascii')
    sections[name] = data[entry[4]:entry[4]+entry[5]]
content = sections['.text']
assert len(content) == expected_width + padding + 2, (len(content), expected_width, padding)
if expected_width == 2:
    half = struct.unpack_from('<H', content)[0]
    assert (half & 3) != 3, hex(half)
elif expected_width == 4:
    word = struct.unpack_from('<I', content)[0]
    assert (word & 0x7f) == 0x63 and ((word >> 12) & 7) == 1, hex(word)
else:
    first, second = struct.unpack_from('<II', content)
    assert (first & 0x7f) == 0x63 and ((first >> 12) & 7) == 0, hex(first)
    assert (second & 0x7f) == 0x6f and ((second >> 7) & 31) == 0, hex(second)
if case_name == 'source_replay_long':
    assert sections['.data'] == struct.pack('<I', expected_width), sections['.data']
PY
  echo "MINIAS_RV_TIERED_RELAX_STAGE=PASS case=$case_name bytes=$width"
done
echo 'MINIAS_RV_TIERED_RELAX=PASS sizes=2,4,8 double_growth=1 source_replay_long=1'

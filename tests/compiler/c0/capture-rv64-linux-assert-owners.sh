#!/usr/bin/env bash
# Diagnostic replay ONLY after a failed real 2064-object GNU Linux link.
# Restore every original MiniC .o/.cmd after a bounded one-owner GNU-E /
# MiniC-S / GNU-AS replay; this must never masquerade as a new producer.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
if (($# != 5)); then
  echo "usage: $0 <linux-src> <linux-out> <exact-minic> <attested-minic-pool> <evidence-dir>" >&2
  exit 2
fi
src=$1
out=$2
minic=$3
pool=$4
evidence=$5

[[ -f "$src/Makefile" && -f "$out/.config" && -x "$minic" &&
   -x "$repo/tests/external/linux/stage2_kbuild_cc.sh" ]] || {
  echo 'ASSERT_SOURCE_REPLAY=FAIL missing_pinned_linux_or_compiler' >&2
  exit 4
}
command -v riscv64-linux-gnu-gcc >/dev/null || {
  echo 'ASSERT_SOURCE_REPLAY=FAIL missing_gnu_cross_cc' >&2
  exit 4
}
mkdir -p "$evidence"
backup=$(mktemp -d)
owners=(
  kernel/pid_namespace.o
  mm/memfd.o
  net/ipv4/tcp_input.o
  net/ipv4/tcp_output.o
  net/ipv4/tcp_timer.o
  net/ipv4/icmp.o
  net/ipv6/exthdrs.o
)

# Before touching the original Kbuild output, attest all seven candidate
# objects against the immutable seven-shard MiniC pool. Other 2057 owners
# stay untouched; this replay never substitutes one GCC-generated C object.
for owner in "${owners[@]}"; do
  [[ -s "$out/$owner" && -s "$pool/$owner" ]] &&
    cmp -s "$out/$owner" "$pool/$owner" || {
      echo "ASSERT_SOURCE_REPLAY=FAIL not_original_minic_owner=$owner" >&2
      exit 8
    }
done

# Kbuild uses the original golden .o.cmd file as its source-command oracle.
# Only one target may be absent at any time; trap restores every .o/.cmd
# byte-for-byte even on an interrupted or rejected replay.
restore() {
  local owner stem leaf original_cmd saved
  for owner in "${owners[@]}"; do
    stem=${owner%.o}
    leaf=${owner##*/}
    original_cmd="$out/$(dirname "$owner")/.${leaf}.cmd"
    saved="$backup/$owner"
    if [[ -s "$saved" ]]; then
      cp -a -- "$saved" "$out/$owner"
    fi
    if [[ -e "$backup/${stem}.cmd" ]]; then
      cp -a -- "$backup/${stem}.cmd" "$original_cmd"
    fi
  done
}
trap 'restore; rm -r -- "$backup"' EXIT

failure=0
for owner in "${owners[@]}"; do
  stem=${owner%.o}
  leaf=${owner##*/}
  original_cmd="$out/$(dirname "$owner")/.${leaf}.cmd"
  [[ -s "$original_cmd" ]] || {
    echo "ASSERT_SOURCE_REPLAY=FAIL missing_gnu_command=$owner" >&2
    failure=1
    continue
  }
  mkdir -p "$backup/$(dirname "$owner")" "$evidence/$(dirname "$owner")"
  cp -a -- "$out/$owner" "$backup/$owner"
  cp -a -- "$original_cmd" "$backup/${stem}.cmd"

  # Explicitly invalidate only this single C owner. The original Kbuild
  # Makefile, compiler options, generated headers and original source path
  # remain unmodified; the wrapper performs GNU -E -> MiniC -S -> GNU AS.
  rm -- "$out/$owner" "$original_cmd"
  if ! MINIC="$minic" REAL_CC=riscv64-linux-gnu-gcc \
       MINIC_KEEP_INTERMEDIATES=1 MINIC_PRESERVE_FAILURE_INPUTS=1 \
       timeout 120s make -C "$src" O="$out" ARCH=riscv \
         CROSS_COMPILE=riscv64-linux-gnu- \
         CC="$repo/tests/external/linux/stage2_kbuild_cc.sh" \
         -j1 "$owner" >"$evidence/${stem//\//_}.replay.log" 2>&1; then
    echo "ASSERT_SOURCE_REPLAY=FAIL make_target=$owner" >&2
    failure=1
  fi

  for ext in i s minic.stderr; do
    generated="$out/${stem}.minic-stage2.$ext"
    if [[ -s "$generated" || "$ext" == minic.stderr && -f "$generated" ]]; then
      cp -a -- "$generated" "$evidence/${stem}.$ext"
      if [[ "$ext" == i ]]; then
        gzip -n -9 "$evidence/${stem}.i"
      fi
    elif [[ "$ext" != minic.stderr ]]; then
      echo "ASSERT_SOURCE_REPLAY=MISSING owner=$owner intermediate=$ext" >&2
      failure=1
    fi
  done
  if [[ -s "$out/$owner" ]] && cmp -s "$out/$owner" "$pool/$owner"; then
    echo "ASSERT_SOURCE_REPLAY=PASS owner=$owner same_MiniC_C_bytes=true"
  else
    echo "ASSERT_SOURCE_REPLAY=FAIL owner=$owner changed_candidate_object" >&2
    failure=1
  fi

  # Restore before starting the next owner; the full 2064-object union is
  # never left contaminated by a diagnostic compiler wrapper command.
  restore
  cmp -s "$out/$owner" "$pool/$owner" || {
    echo "ASSERT_SOURCE_REPLAY=FAIL restoration_mismatch=$owner" >&2
    exit 8
  }
done

if ((failure)); then
  echo 'ASSERT_SOURCE_REPLAY=INCONCLUSIVE frozen_input_or_bytes_mismatch' >&2
  exit 8
fi
echo 'ASSERT_SOURCE_REPLAY=PASS owners=7 no_GCC_C_fallback=true object_bytes_restored=true'

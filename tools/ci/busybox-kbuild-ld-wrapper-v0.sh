#!/usr/bin/env bash
set -Eeuo pipefail

: "${BUSYBOX_MINI_LD:?}"
: "${BUSYBOX_MINI_LD_TRACE:?}"

{
  flock 9
  printf 'ld' >&9
  printf ' %q' "$@" >&9
  printf '\n' >&9
} 9>>"$BUSYBOX_MINI_LD_TRACE"

filtered=()
for arg in "$@"; do
  case "$arg" in
    -nostdlib) ;;
    *) filtered+=("$arg") ;;
  esac
done

exec "$BUSYBOX_MINI_LD" -melf64lriscv "${filtered[@]}"

#!/usr/bin/env bash
set -Eeuo pipefail

: "${BUSYBOX_MINI_AR:?}"
: "${BUSYBOX_MINI_AR_TRACE:?}"

{
  flock 9
  printf 'ar' >&9
  printf ' %q' "$@" >&9
  printf '\n' >&9
} 9>>"$BUSYBOX_MINI_AR_TRACE"

exec "$BUSYBOX_MINI_AR" "$@"

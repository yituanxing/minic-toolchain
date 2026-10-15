#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <full-linux-out> <compact-out>" >&2
  exit 64
fi

src=$(realpath "$1")
dst=$(realpath -m "$2")
AR=${AR:-riscv64-linux-gnu-ar}
rm -rf "$dst"
mkdir -p "$dst"

declare -A seen=()
manifest=$(mktemp)
trap 'rm -f "$manifest"' EXIT

add_path() {
  local rel=${1#./}
  [[ -n "$rel" ]] || return 0
  [[ -z ${seen[$rel]+x} ]] || return 0
  test -e "$src/$rel" || {
    echo "LINK_FIXTURE_MISSING=$rel" >&2
    return 1
  }
  seen[$rel]=1
  printf '%s\n' "$rel" >>"$manifest"
}

add_archive_graph() {
  local archive=${1#./}
  add_path "$archive"
  local member rel
  while IFS= read -r member; do
    [[ -n "$member" ]] || continue
    rel=${member#./}
    if [[ -e "$src/$rel" ]]; then
      add_path "$rel"
    elif [[ -e "$src/$(dirname "$archive")/$rel" ]]; then
      add_path "$(dirname "$archive")/$rel"
    else
      echo "LINK_FIXTURE_ARCHIVE_MEMBER_MISSING archive=$archive member=$member" >&2
      return 1
    fi
  done < <(cd "$src" && "$AR" t "$archive")
}

# Metadata and terminal link inputs consumed directly by linux-early-runtime-link-v2.sh.
for rel in \
  .config \
  include/config/auto.conf \
  arch/riscv/kernel/vmlinux.lds \
  init/version-timestamp.o; do
  add_path "$rel"
done

# The terminal link consumes these Linux thin archives.  Preserve the archive
# files plus every referenced member, keeping paths exactly as in the certified
# Kbuild output tree.
for archive in \
  built-in.a \
  lib/lib.a \
  arch/riscv/lib/lib.a \
  drivers/firmware/efi/libstub/lib.a; do
  add_archive_graph "$archive"
done

# Frontier owners must always be explicit even if archive enumeration changes.
add_path lib/idr.o
add_path lib/xarray.o

LC_ALL=C sort -u "$manifest" -o "$manifest"
(
  cd "$src"
  tar -cf - -T "$manifest"
) | (
  cd "$dst"
  tar -xf -
)

cp "$manifest" "$dst/link-files.txt"
(
  cd "$dst"
  while IFS= read -r rel; do sha256sum "$rel"; done <link-files.txt
) >"$dst/link-files.sha256"

files=$(wc -l <"$manifest" | tr -d ' ')
bytes=$(du -sb "$dst" | awk '{print $1}')
printf 'COMPACT_LINK_FIXTURE=PASS files=%s bytes=%s\n' "$files" "$bytes"

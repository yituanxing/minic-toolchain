#!/usr/bin/env bash
# ELF serialization is transactional: an I/O or rename failure must not
# destroy the previously accepted object or leave a partial temporary file.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mini=${MINIAS:-"$root/build/release/bin/minic-as"}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/large.s" <<'ASM'
.text
.globl atomic_output
atomic_output:
  .fill 8192, 1, 0x90
ASM

output="$work/protected.o"
printf 'previous valid object\n' >"$output"
chmod 640 "$output"
original=$(sha256sum "$output" | cut -d' ' -f1)

# Limit the assembler's regular-file output to one KiB, while preventing
# SIGXFSZ from terminating before the fwrite/fclose cleanup path executes.
if (ulimit -f 1; trap '' XFSZ; "$mini" --target=x86_64 \
    -o "$output" "$work/large.s") >"$work/fail.log" 2>&1; then
    echo 'MiniAS unexpectedly accepted a truncated ELF output' >&2
    exit 1
fi
grep -q 'output-write:' "$work/fail.log"
test "$(sha256sum "$output" | cut -d' ' -f1)" = "$original"
test "$(stat -c '%a' "$output")" = 640
test -z "$(find "$work" -maxdepth 1 -name '*.minias-tmp-*' -print -quit)"

# Non-regular destinations retain their original stream behavior: in
# particular, an output directory must never be replaced by an ELF image.
mkdir "$work/existing-directory"
if "$mini" --target=x86_64 -o "$work/existing-directory" \
    "$work/large.s" >"$work/rename.log" 2>&1; then
    echo 'MiniAS unexpectedly replaced an output directory' >&2
    exit 1
fi
grep -q 'output-open:' "$work/rename.log"
test -d "$work/existing-directory"
test -z "$(find "$work" -maxdepth 1 -name '*.minias-tmp-*' -print -quit)"

# Linux Kbuild deliberately assembles feature probes into /dev/null, which
# must continue to behave as a writable device instead of being renamed.
"$mini" --target=x86_64 -o /dev/null "$work/large.s"
ln -s protected.o "$work/linked.o"
"$mini" --target=x86_64 -o "$work/linked.o" "$work/large.s"
test -L "$work/linked.o"
readelf -h "$output" | grep -q 'REL (Relocatable file)'

# Successful replacement keeps the existing file's permissions, whereas
# a new output honors the caller's umask, just like open(O_CREAT, 0666).
"$mini" --target=x86_64 -o "$output" "$work/large.s"
test "$(stat -c '%a' "$output")" = 640
readelf -h "$output" | grep -q 'REL (Relocatable file)'
(umask 077; "$mini" --target=x86_64 -o "$work/new.o" "$work/large.s")
test "$(stat -c '%a' "$work/new.o")" = 600
test -z "$(find "$work" -maxdepth 1 -name '*.minias-tmp-*' -print -quit)"
echo 'MINIAS_ATOMIC_OUTPUT=PASS write_fail=1 nonregular_fail=1 device_output=1 symlink_output=1 old_object=preserved mode=preserved umask=honored'

#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-call-frame-address
assembly="$work/gnu_call_frame_address.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_call_frame_address.c" \
    -o "$work/gnu_call_frame_address.i"
"$minic" -S "$work/gnu_call_frame_address.i" -o "$assembly"

test -s "$assembly"
grep -F 'capture_return_after_call:' "$assembly" >/dev/null
grep -F 'capture_frame_address:' "$assembly" >/dev/null
grep -F 'linux_return_address_shape:' "$assembly" >/dev/null
grep -F '  call clobber_ra' "$assembly" >/dev/null

saved_ra_offset=$(awk '
    /^capture_return_after_call:$/ { inside=1; next }
    inside && /^\.Lcapture_return_after_call_return:$/ { exit }
    inside && /^  sd ra, [0-9]+\(sp\)$/ {
        line=$0
        sub(/^  sd ra, /, "", line)
        sub(/\(sp\)$/, "", line)
        print line
        exit
    }
' "$assembly")
test -n "$saved_ra_offset"
awk -v offset="$saved_ra_offset" '
    /^capture_return_after_call:$/ { inside=1; next }
    inside && /^\.Lcapture_return_after_call_return:$/ { exit }
    inside && $0 == "  ld a0, " offset "(s0)" { found=1 }
    END { exit found ? 0 : 1 }
' "$assembly"
if awk '
    /^capture_return_after_call:$/ { inside=1; next }
    inside && /^\.Lcapture_return_after_call_return:$/ { exit }
    inside && /^  mv a0, ra$/ { found=1 }
    END { exit found ? 0 : 1 }
' "$assembly"; then
    printf '%s\n' '__builtin_return_address(0) incorrectly read live ra' >&2
    exit 1
fi
awk '
    /^capture_frame_address:$/ { inside=1; next }
    inside && /^\.Lcapture_frame_address_return:$/ { exit }
    inside && /^  mv a0, s0$/ { found=1 }
    END { exit found ? 0 : 1 }
' "$assembly"
if grep -E '__builtin_(return|frame)_address' "$assembly" >/dev/null; then
    printf '%s\n' 'call-frame builtin spelling leaked into emitted assembly' >&2
    exit 1
fi

cat >"$work/nonconstant.c" <<'EOF'
void *bad_level(int level) {
    return __builtin_return_address(level);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nonconstant.c" -o "$work/nonconstant.i"
if "$minic" -S "$work/nonconstant.i" -o "$work/nonconstant.s" \
    >"$work/nonconstant.stdout" 2>"$work/nonconstant.stderr"; then
    printf '%s\n' 'nonconstant return-address level unexpectedly compiled' >&2
    exit 1
fi
grep -F '__builtin_return_address level must be an integer constant' \
    "$work/nonconstant.stderr" >/dev/null

cat >"$work/nonzero.c" <<'EOF'
void *bad_return_level(void) {
    return __builtin_return_address(1);
}
void *bad_frame_level(void) {
    return __builtin_frame_address(1);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nonzero.c" -o "$work/nonzero.i"
if "$minic" -S "$work/nonzero.i" -o "$work/nonzero.s" \
    >"$work/nonzero.stdout" 2>"$work/nonzero.stderr"; then
    printf '%s\n' 'nonzero call-frame level unexpectedly compiled' >&2
    exit 1
fi
grep -F '__builtin_return_address level is not supported by target' \
    "$work/nonzero.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_call_frame_address return-level0=saved-entry-ra frame-level0=s0 nonconstant=reject nonzero=reject'

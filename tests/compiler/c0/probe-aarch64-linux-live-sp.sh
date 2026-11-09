#!/usr/bin/env bash
# Linux 6.6 arm64 stacktrace/current_stack_pointer fixed GNU register alias.
# The target must read the live hardware SP; it must never permit writing it.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/live-sp.i" <<'C'
register unsigned long current_stack_pointer asm("sp");
unsigned long aarch_stack_snapshot(void) {
    return current_stack_pointer;
}
C
"$mini" --target=aarch64-linux -S "$work/live-sp.i" -o "$work/live-sp.s"
test "$(grep -Ec '^  mov x9, sp$' "$work/live-sp.s")" -eq 1
echo 'AARCH64_LIVE_SP=PASS physical=mov_x9_sp global_register_alias=sp'
cat > "$work/illegal-sp-write.i" <<'C'
register unsigned long current_stack_pointer asm("sp");
void bad_sp_write(unsigned long input) { current_stack_pointer = input; }
C
cat > "$work/illegal-x29.i" <<'C'
register unsigned long frame_pointer asm("x29");
unsigned long bad_saved_register(void) { return frame_pointer; }
C
cat > "$work/illegal-typemismatch.i" <<'C'
register unsigned int short_sp asm("sp");
unsigned int bad_sp_width(void) { return short_sp; }
C
for invalid in illegal-sp-write illegal-x29 illegal-typemismatch; do
  if "$mini" --target=aarch64-linux -S "$work/$invalid.i" -o "$work/$invalid.s" \
      >/dev/null 2>"$work/$invalid.err"; then
    echo "AARCH64_LIVE_SP=FAIL invalid case $invalid was admitted" >&2
    exit 1
  fi
  test ! -e "$work/$invalid.s"
done
echo 'AARCH64_LIVE_SP_NEGATIVE=PASS write=suppressed x29=blocked width=checked'

#!/usr/bin/env bash
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
here=$(cd "$(dirname "$0")" && pwd)
src="$here/loongarch64_packed_uaccess_output.c"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$src"
cc -E -P -x c "$src" -o "$d/source.i"
"$mini" --target=loongarch64-linux -S "$d/source.i" -o "$d/source.s"

for entry in byte:1:b half:2:h word:4:w wide_carrier:8:d; do
  IFS=: read -r name width suffix <<<"$entry"
  name="packed_uaccess_$name"
  sed -n "/^${name}:/,/^\.size ${name},/p" "$d/source.s" > "$d/$name.s"
  test -s "$d/$name.s"
  grep -Fq '.pushsection __ex_table, "a"' "$d/$name.s"
  grep -Fq '.short ((12 << 0) | (13 << 5))' "$d/$name.s"
  grep -Fq 'andi $t3, $t1, 3' "$d/$name.s"  # packed +r ingress
  grep -Fq 'ld.bu $t2, $t1, 0' "$d/$name.s"
  grep -Fq 'andi $t3, $t7, 3' "$d/$name.s"  # packed +r egress
  grep -Fq 'st.w $t0, $t7, 0' "$d/$name.s"
  grep -Fq "  st.$suffix \$t1, \$t7, 0" "$d/$name.s"
  grep -Fq 'st.b $t3, $t7,' "$d/$name.s"
  if [[ "$width" != 8 ]]; then
    # The result writeback must no longer overwrite 8 bytes of neighboring
    # packed fields; only the typed-sized native fallback is permitted.
    if grep -Fq 'st.d $t1, $t7, 0' "$d/$name.s"; then
      echo "LOONG_PACKED_UACCESS=FAIL wide result store for $name" >&2
      exit 1
    fi
  fi
done
grep -Fq '1: ld.b $t1, $t4, 0' "$d/packed_uaccess_byte.s"
grep -Fq '1: ld.h $t1, $t4, 0' "$d/packed_uaccess_half.s"
grep -Fq '1: ld.w $t1, $t4, 0' "$d/packed_uaccess_word.s"

# Original GNU Linux __get_user/__put_user routes their memory address
# through struct __large_struct, whose 800-byte size is unrelated to the
# faulting 1/2/4/8-byte load/store. Require all eight native target forms.
for entry in get:byte:ld.b get:half:ld.h get:word:ld.w get:double:ld.d \
             put:byte:st.b put:half:st.h put:word:st.w put:double:st.d; do
  IFS=: read -r action width opcode <<<"$entry"
  name="linux_large_${action}_${width}"
  sed -n "/^${name}:/,/^\.size ${name},/p" "$d/source.s" > "$d/$name.s"
  test -s "$d/$name.s"
  grep -Fq "1: $opcode " "$d/$name.s"
  grep -Fq '.pushsection __ex_table, "a"' "$d/$name.s"
  if [[ "$action" == get ]]; then
    grep -Fq '.short ((12 << 0) | (13 << 5))' "$d/$name.s"
  else
    grep -Fq '.short ((12 << 0) | (0 << 5))' "$d/$name.s"
  fi
done

for bad in wrong-memory-width narrow-register-output injected-asm fake-sentinel-record; do
  case "$bad" in
    wrong-memory-width)
      sed 's/"ld.b"/"ld.w"/' "$d/source.i" > "$d/bad.i" ;;
    narrow-register-output)
      sed 's/"=r"(r->word)/"=r"(r->byte)/' "$d/source.i" > "$d/bad.i" ;;
    injected-asm)
      sed 's/"ld.b"/"ld.b; nop"/' "$d/source.i" > "$d/bad.i" ;;
    fake-sentinel-record)
      sed 's/__large_struct/__wrong_large_struct/g' "$d/source.i" > "$d/bad.i" ;;
  esac
  if "$mini" --target=loongarch64-linux -S "$d/bad.i" \
      -o "$d/bad.s" >/dev/null 2>&1; then
    echo "LOONG_PACKED_UACCESS=FAIL unqualified $bad" >&2
    exit 1
  fi
  test ! -e "$d/bad.s"
done

echo 'LOONG_PACKED_UACCESS=PASS GNU_C=PASS typed_carriers=1,2,4,8 original_large_struct_get_put=8 guarded_error_ingress_egress=1 exception_table=preserved negatives=4 QEMU=NOT_RUN'

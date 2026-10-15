#!/bin/sh
set -eu

: "${MININM:?MININM must point to minic-nm}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
AR=${RISCV_AR:-riscv64-linux-gnu-ar}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}

work="$BUILD_DIR/tests/nm/a1"
rm -rf "$work"
mkdir -p "$work/obj"

cat >"$work/obj/first_member_with_long_name.s" <<'EOF'
.text
.globl alpha
.type alpha, @function
alpha:
  call beta
  ret
.size alpha, .-alpha

.local alpha_local
.type alpha_local, @function
alpha_local:
  ret
.size alpha_local, .-alpha_local
EOF

cat >"$work/obj/second.s" <<'EOF'
.text
.globl beta
.type beta, @function
beta:
  ret
.size beta, .-beta

.data
.globl payload
.type payload, @object
payload:
  .word 77
.size payload, 4
EOF

"$AS" -march=rv64imac -mabi=lp64   -o "$work/obj/first_member_with_long_name.o"   "$work/obj/first_member_with_long_name.s"
"$AS" -march=rv64imac -mabi=lp64   -o "$work/obj/second.o"   "$work/obj/second.s"

"$AR" crs "$work/regular.a"   "$work/obj/first_member_with_long_name.o"   "$work/obj/second.o"
(
  cd "$work"
  "$AR" crsT thin.a     obj/first_member_with_long_name.o     obj/second.o
)

compare_archive() {
  label="$1"
  archive="$2"
  shift 2

  LC_ALL=C "$NM" "$@" "$archive" >"$work/$label.gnu"
  LC_ALL=C "$MININM" "$@" "$archive" >"$work/$label.mini"

  if ! cmp "$work/$label.gnu" "$work/$label.mini"; then
    echo "MININM_A1_DIFF case=$label" >&2
    diff -u "$work/$label.gnu" "$work/$label.mini" >&2 || true
    exit 1
  fi
  echo "MININM_A1_CASE=PASS case=$label"
}

for archive_kind in regular thin; do
  archive="$work/$archive_kind.a"
  compare_archive "$archive_kind-default" "$archive"
  compare_archive "$archive_kind-numeric" "$archive" -n
  compare_archive "$archive_kind-global" "$archive" -g
  compare_archive "$archive_kind-undefined" "$archive" -u
done

echo "MININM_A1=PASS oracle=GNU-nm archives=regular,thin long-name=PASS options=default,-n,-g,-u"

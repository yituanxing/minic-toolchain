#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-gcc}
work=${BUILD_DIR:-"$root/build/release"}/tests/netlink-warn-branch
source="$root/tests/compiler/c0/netlink_warn_x86_minimal.c"

mkdir -p "$work"
"$host_cc" -E -P -x c "$source" -o "$work/netlink.i"
"$minic" -S "$work/netlink.i" --target=x86_64-linux -o "$work/netlink.minic.s"
"$host_cc" -static -no-pie "$work/netlink.minic.s" -o "$work/netlink.minic"
"$host_cc" -static -no-pie "$source" -o "$work/netlink.gnu"

"$work/netlink.gnu"
"$work/netlink.minic"
printf '%s\n' 'MINIC_NETLINK_WARN_BRANCH=PASS gnu=0 minic=0'

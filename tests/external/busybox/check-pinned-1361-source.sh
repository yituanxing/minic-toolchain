#!/bin/sh
# Shared immutable original BusyBox 1.36.1 source identity for all targets.
# The tarball commit URL is not by itself a source SHA attestation.
set -eu
test "$#" -eq 1
root=$1
# Reuse exact pinned source identities from the accepted x86 fixture.
for item in \
  Makefile:fa8cb761443388730372eaf260e7ca1a0235bc43 \
  coreutils/cat.c:4b3414941e87a47bd639bc481ab9f3fa0045ea80 \
  coreutils/echo.c:44b2eb5d05fac12374b5f8692990014087cfda01 \
  coreutils/head.c:c7537a20e4e01483f3a6ffaf89eb2da25d7acf5f \
  coreutils/wc.c:d5238d3fcfba2f62f6bb292a3af55c5107c3ec93 \
  findutils/grep.c:0b72812f18b9177855e226c78f3625146b8daee2 \
  coreutils/cut.c:55bdd938611feb51e7bf1b7977f87015b2819cf9 \
  coreutils/sort.c:01b7c44e5e2cc72027269848b9c025abe45c7506 \
  coreutils/uniq.c:06c57f750be6a94de5189495688a3150385166aa \
  coreutils/tr.c:1e402dfdbd4f047677b8f72ac884eb6e38110fb5; do
  rel=${item%%:*}; expected=${item#*:}
  actual=$(git hash-object "$root/$rel")
  if [ "$actual" != "$expected" ]; then
    echo "BUSYBOX_PINNED_SOURCE=FAIL file=$rel expected=$expected actual=$actual" >&2
    exit 1
  fi
done
echo "BUSYBOX_PINNED_SOURCE=PASS release=1.36.1 commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4"

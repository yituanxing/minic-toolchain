#!/usr/bin/env bash
# Kbuild LD= adapter: GNU BFD remains the linker of record.  Capture the
# *first successful* real Linux kallsyms1 final-link argv for an independent
# MiniLD replay after Kbuild has completed.  No command is evaluated as text.
set -Eeuo pipefail

oracle=${MINILD_LINUX_GNU_LD:-}
capture=${MINILD_LINUX_FINAL_CAPTURE_DIR:-}
if [[ -z $oracle || -z $capture || $# -eq 0 ]]; then
  echo 'MINILD_LINUX_CAPTURE=FAIL require MINILD_LINUX_GNU_LD, MINILD_LINUX_FINAL_CAPTURE_DIR and GNU ld argv' >&2
  exit 64
fi
command -v -- "$oracle" >/dev/null || {
  echo "MINILD_LINUX_CAPTURE=FAIL missing_GNU_ld=$oracle" >&2
  exit 65
}

output=''
want_output=false
for arg in "$@"; do
  if $want_output; then
    output=$arg
    want_output=false
    continue
  fi
  case "$arg" in
    -o|--output) want_output=true ;;
    --output=*) output=${arg#--output=} ;;
    -o?*) output=${arg#-o} ;;
  esac
done

# The non-final linker invocations are deliberately untouched and do not
# create evidence.  Kbuild may execute many ordinary LD relocatable links.
if [[ ${output##*/} != .tmp_vmlinux.kallsyms1 ]]; then
  exec "$oracle" "$@"
fi

gnu_status=0
"$oracle" "$@" || gnu_status=$?
if [[ $gnu_status -ne 0 ]]; then
  echo "MINILD_LINUX_CAPTURE=FAIL GNU_kallsyms1_rc=$gnu_status" >&2
  exit "$gnu_status"
fi

mkdir -p -- "$capture"
capture=$(realpath -- "$capture")
if [[ -e $capture/first-kallsyms1.argv0 ]]; then
  echo 'MINILD_LINUX_CAPTURE=UNCHANGED first_success_already_recorded=true'
  exit 0
fi

# Capture is recorded only after successful GNU BFD linkage.  The argv and
# working directory are separate because null-delimited argv has no reliable
# textual shell representation; replay must use mapfile -d '' (never eval).
temp=$(mktemp -d "$capture/.first-kallsyms1.XXXXXXXX")
trap 'rm -r -- "$temp"' EXIT
printf '%s\0' "$@" >"$temp/argv0"
pwd -P >"$temp/cwd"
printf '%s\n' "$(command -v -- "$oracle")" >"$temp/gnu-ld"
printf '%s\n' "$output" >"$temp/original-output"
sha256sum "$(command -v -- "$oracle")" >"$temp/gnu-ld.sha256"
if [[ -s $output ]]; then
  sha256sum "$output" >"$temp/original-output.sha256"
else
  echo "MINILD_LINUX_CAPTURE=FAIL GNU_success_but_output_missing=$output" >&2
  exit 66
fi
mv -- "$temp/argv0" "$capture/first-kallsyms1.argv0"
mv -- "$temp/cwd" "$capture/first-kallsyms1.cwd"
mv -- "$temp/gnu-ld" "$capture/first-kallsyms1.gnu-ld"
mv -- "$temp/original-output" "$capture/first-kallsyms1.original-output"
mv -- "$temp/gnu-ld.sha256" "$capture/first-kallsyms1.gnu-ld.sha256"
mv -- "$temp/original-output.sha256" "$capture/first-kallsyms1.original-output.sha256"
echo 'MINILD_LINUX_CAPTURE=PASS first_real_GNU_kallsyms1=true argv0=true'

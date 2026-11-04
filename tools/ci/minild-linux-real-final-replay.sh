#!/usr/bin/env bash
# Replays one *real GNU Kbuild* kallsyms1 command, recorded by
# minild-linux-real-final-capture-ld.sh, with GNU BFD and MiniLD on the same
# frozen GNU-produced inputs.  This is not a vmlinux/Image/QEMU P1 verdict.
#
# Usage: bash tools/ci/minild-linux-real-final-replay.sh \
#   <capture-dir> <minic-ld-bin> <evidence-dir>
set -Eeuo pipefail

if [[ $# != 3 ]]; then
  echo 'usage: minild-linux-real-final-replay.sh <capture-dir> <minic-ld-bin> <evidence-dir>' >&2
  exit 64
fi
capture=$(realpath -m -- "$1")
mini=$(realpath -m -- "$2")
evidence=$(realpath -m -- "$3")
mkdir -p -- "$evidence"
for suffix in argv0 cwd gnu-ld original-output gnu-ld.sha256; do
  [[ -s $capture/first-kallsyms1.$suffix ]] || {
    echo "MINILD_LINUX_FINAL_REPLAY=INCONCLUSIVE missing_GNU_capture=$suffix" >&2
    exit 65
  }
done
[[ -x $mini ]] || {
  echo "MINILD_LINUX_FINAL_REPLAY=INCONCLUSIVE missing_MiniLD=$mini" >&2
  exit 66
}
mapfile -d '' -t original <"$capture/first-kallsyms1.argv0"
[[ ${#original[@]} -ge 5 ]] || {
  echo 'MINILD_LINUX_FINAL_REPLAY=INCONCLUSIVE empty_or_short_kallsyms1_argv' >&2
  exit 67
}
cwd=$(<"$capture/first-kallsyms1.cwd")
gnu=$(<"$capture/first-kallsyms1.gnu-ld")
[[ -d $cwd && -x $gnu ]] || {
  echo 'MINILD_LINUX_FINAL_REPLAY=INCONCLUSIVE missing_Kbuild_tree_or_GNU_linker' >&2
  exit 68
}
sha256sum --quiet --check "$capture/first-kallsyms1.gnu-ld.sha256" || {
  echo 'MINILD_LINUX_FINAL_REPLAY=FAIL GNU_tool_changed' >&2
  exit 69
}

# Preserve replay provenance before trying either linker.  Candidate inputs
# include direct object/archive arguments and the target-owned linker script.
# All paths are resolved relative to the original Kbuild linker cwd.  This
# intentionally fails closed when command-line inputs disappeared after make.
declare -A known_inputs=()
have_object=false
have_archive=false
have_script=false
expect_script=false
(
  cd -- "$cwd"
  for arg in "${original[@]}"; do
    candidate=''
    if $expect_script; then
      candidate=$arg
      expect_script=false
    else
      case "$arg" in
        -T|--script|--version-script|--just-symbols) expect_script=true ;;
        --script=*|--version-script=*|--just-symbols=*) candidate=${arg#*=} ;;
        -T?*) candidate=${arg#-T} ;;
        *.o|*.a|*.lds|*.ld) [[ $arg != -* ]] && candidate=$arg ;;
      esac
    fi
    [[ -n $candidate ]] || continue
    [[ -f $candidate ]] || {
      echo "MINILD_LINUX_FINAL_REPLAY=FAIL link_input_missing=$candidate" >&2
      exit 70
    }
    full=$(realpath -- "$candidate")
    [[ -z ${known_inputs[$full]:-} ]] || continue
    known_inputs[$full]=1
    sha256sum -- "$full"
  done
) >"$evidence/input-objects-and-scripts.sha256"

# The captured link must actually be a kernel final-link invocation.  A
# small test ELF or one intermediate '-r' link does not meet this criterion.
for arg in "${original[@]}"; do
  case "$arg" in
    *.o) have_object=true ;;
    *.a) have_archive=true ;;
    *.lds|*.ld|--script=*|-T*) have_script=true ;;
  esac
done
if ! $have_object || ! $have_archive || ! $have_script; then
  echo 'MINILD_LINUX_FINAL_REPLAY=INCONCLUSIVE no_real_kernel_archive_objects_or_linker_script' >&2
  exit 71
fi
sha256sum --quiet -c "$evidence/input-objects-and-scripts.sha256" || {
  echo 'MINILD_LINUX_FINAL_REPLAY=FAIL frozen_GNU_inputs_changed' >&2
  exit 72
}

gnu_out="$evidence/.tmp_vmlinux.kallsyms1.gnu"
mini_out="$evidence/.tmp_vmlinux.kallsyms1.minild"
gnu_args=("${original[@]}")
mini_args=("${original[@]}")
replaced=0
for ((i=0; i<${#original[@]}; i++)); do
  arg=${original[i]}
  case "$arg" in
    -o|--output)
      ((i + 1 < ${#original[@]})) || exit 73
      [[ ${original[i+1]##*/} == .tmp_vmlinux.kallsyms1 ]] || exit 73
      gnu_args[i+1]=$gnu_out
      mini_args[i+1]=$mini_out
      replaced=$((replaced+1))
      i=$((i+1))
      ;;
    --output=*|-o?*)
      [[ ${arg##*/} == .tmp_vmlinux.kallsyms1 ]] || exit 73
      if [[ $arg == --output=* ]]; then
        gnu_args[i]=--output="$gnu_out"
        mini_args[i]=--output="$mini_out"
      else
        gnu_args[i]=-o"$gnu_out"
        mini_args[i]=-o"$mini_out"
      fi
      replaced=$((replaced+1))
      ;;
  esac
done
if [[ $replaced != 1 ]]; then
  echo "MINILD_LINUX_FINAL_REPLAY=INCONCLUSIVE output_option_count=$replaced" >&2
  exit 73
fi
printf '%s\0' "${gnu_args[@]}" >"$evidence/gnu-replay.argv0"
printf '%s\0' "${mini_args[@]}" >"$evidence/minild-replay.argv0"
printf 'capture=%s\nKbuild_cwd=%s\noriginal_GNU_ld=%s\nMiniLD=%s\n' \
  "$capture" "$cwd" "$gnu" "$mini" >"$evidence/replay-tools-and-cwd.txt"
sha256sum "$mini" >"$evidence/minild.sha256"

# Re-execute GNU for an independent same-input control, then MiniLD.  All
# stdout/stderr and even failing outputs stay in the evidence directory.
gnu_status=0
(cd -- "$cwd" && timeout "${MINILD_LINUX_LINK_TIMEOUT:-240s}" \
    "$gnu" "${gnu_args[@]}") >"$evidence/gnu.stdout" \
    2>"$evidence/gnu.stderr" || gnu_status=$?
printf '%s\n' "$gnu_status" >"$evidence/gnu.status"
if [[ $gnu_status != 0 || ! -s $gnu_out ]]; then
  echo "MINILD_LINUX_FINAL_REPLAY=FAIL GNU_replay_status=$gnu_status" >&2
  exit 74
fi
sha256sum --quiet -c "$evidence/input-objects-and-scripts.sha256" || {
  echo 'MINILD_LINUX_FINAL_REPLAY=FAIL GNU_replay_mutated_inputs' >&2
  exit 75
}
mini_status=0
(cd -- "$cwd" && timeout "${MINILD_LINUX_LINK_TIMEOUT:-240s}" \
    "$mini" "${mini_args[@]}") >"$evidence/minild.stdout" \
    2>"$evidence/minild.stderr" || mini_status=$?
printf '%s\n' "$mini_status" >"$evidence/minild.status"
if [[ $mini_status != 0 || ! -s $mini_out ]]; then
  echo "MINILD_LINUX_FINAL_REPLAY=BLOCKED mini_status=$mini_status real_kallsyms1=true GNU_control=PASS" >&2
  tail -n 30 "$evidence/minild.stderr" >&2 || true
  exit 76
fi

readelf -W -h "$gnu_out" >"$evidence/gnu-elf-header.txt"
readelf -W -h "$mini_out" >"$evidence/mini-elf-header.txt"
for label in 'Class:' 'Data:' 'Type:' 'Machine:'; do
  gnu_value=$(sed -n "s/^[[:space:]]*${label}[[:space:]]*//p" "$evidence/gnu-elf-header.txt")
  mini_value=$(sed -n "s/^[[:space:]]*${label}[[:space:]]*//p" "$evidence/mini-elf-header.txt")
  if [[ -z $gnu_value || $gnu_value != "$mini_value" ]]; then
    echo "MINILD_LINUX_FINAL_REPLAY=FAIL ELF_header_mismatch=$label" >&2
    exit 77
  fi
done
sha256sum "$gnu_out" "$mini_out" >"$evidence/replayed-ELF.sha256"
printf 'MINILD_LINUX_FINAL_REPLAY=PASS real_kallsyms1=true GNU_and_MiniLD=true SHA_verified_GNU_Kbuild_inputs=true vmlinux_Image_QEMU=NOT_RUN\n'

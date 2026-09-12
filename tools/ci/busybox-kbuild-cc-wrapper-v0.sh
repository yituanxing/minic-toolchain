#!/usr/bin/env bash
set -u

: "${BUSYBOX_REAL_GCC:=riscv64-linux-gnu-gcc}"
: "${BUSYBOX_MINIC:?}"
: "${BUSYBOX_MINI_SYSROOT:?}"
: "${BUSYBOX_UAPI_COMPAT:?}"
: "${BUSYBOX_MUSL_BUILD:?}"
: "${BUSYBOX_LIBGCC:?}"
: "${BUSYBOX_C_RESULTS:?}"

# Compiler identity/capability probes are build-system control traffic, not
# BusyBox target producers. Keep them on the reference GNU cross driver.
probe_input=0
probe_linker_option=0
for arg in "$@"; do
  case "$arg" in
    -dumpversion|-dumpfullversion|-dumpmachine)
      exec "$BUSYBOX_REAL_GCC" "$@"
      ;;
    /dev/null|-xc|-xassembler)
      probe_input=1
      ;;
    -Wl,*)
      probe_linker_option=1
      ;;
  esac
done
if test "$probe_input" -eq 1; then
  exec "$BUSYBOX_REAL_GCC" \
    --sysroot="$BUSYBOX_MINI_SYSROOT/.." \
    -I"$BUSYBOX_UAPI_COMPAT" -fno-pic -fno-pie "$@"
fi

compile=0
relocatable=0
dep_only=0
source=
output=
depfile=
dep_target=
previous=
for arg in "$@"; do
  case "$arg" in
    -c|-E|-S) compile=1 ;;
    -r) relocatable=1 ;;
    -M|-MM) dep_only=1 ;;
    *.c|*.S|*.s) source="$arg" ;;
  esac
  if test "$previous" = "-o"; then
    output="$arg"
  elif test "$previous" = "-MF"; then
    depfile="$arg"
  elif test "$previous" = "-MT" || test "$previous" = "-MQ"; then
    dep_target="$arg"
  fi
  case "$arg" in
    -Wp,-MD,*) depfile="${arg#-Wp,-MD,}" ;;
    -Wp,-MMD,*) depfile="${arg#-Wp,-MMD,}" ;;
  esac
  previous="$arg"
done

# scripts/trylink probes linker features with temporary C files. Do not send
# these through Mini or add the hosted static link payload to them.
if test "$probe_linker_option" -eq 1 && test "$compile" -eq 1 && test -n "$source"; then
  case "$source" in
    tmp.*.c|*/tmp.*.c)
      exec "$BUSYBOX_REAL_GCC" \
        --sysroot="$BUSYBOX_MINI_SYSROOT/.." \
        -I"$BUSYBOX_UAPI_COMPAT" -fno-pic -fno-pie "$@"
      ;;
  esac
fi

# CC-level relocatable/proprocessor/assembly traffic is not a C-producer
# ownership event. Kbuild's explicit LD/AR variables own aggregation.
if test "$relocatable" -eq 1; then
  exec "$BUSYBOX_REAL_GCC" --sysroot="$BUSYBOX_MINI_SYSROOT/.." -nostdlib "$@"
fi
if test "$dep_only" -eq 1 || test "${source##*.}" = "S" || test "${source##*.}" = "s"; then
  exec "$BUSYBOX_REAL_GCC" \
    --sysroot="$BUSYBOX_MINI_SYSROOT/.." \
    -I"$BUSYBOX_UAPI_COMPAT" -fno-pic -fno-pie "$@"
fi

if test "$compile" -eq 1 && test "${source##*.}" = "c"; then
  filtered=()
  skip=0
  for arg in "$@"; do
    if test "$skip" -eq 1; then
      skip=0
      continue
    fi
    case "$arg" in
      -Wall|-W*|-O|-O0|-O1|-O2|-O3|-Os|-Oz|-Og|-Ofast|-pipe|-g|-g0|-g1|-g2|-g3)
        ;;
      -fno-builtin*|-fno-strict-aliasing|-fomit-frame-pointer|\
      -ffunction-sections|-fdata-sections|-fno-asynchronous-unwind-tables|\
      -fno-unwind-tables|-fno-stack-protector|-fno-pic|-fno-pie|-fno-PIE|\
      -finline-limit=*|-fno-guess-branch-probability|-funsigned-char|\
      -static-libgcc|-falign-functions=*|-falign-jumps=*|-falign-labels=*|\
      -falign-loops=*|-march=*|-mabi=*|-mcmodel=*|-mno-*|-std=*)
        ;;
      -MD|-MMD|-MP)
        ;;
      -MF|-MT|-MQ)
        skip=1
        ;;
      -Wp,*)
        ;;
      *)
        filtered+=("$arg")
        ;;
    esac
  done

  set +e
  timeout "${BUSYBOX_MINIC_TIMEOUT:-120s}" "$BUSYBOX_MINIC" \
    --sysroot "$BUSYBOX_MINI_SYSROOT" -I"$BUSYBOX_UAPI_COMPAT" \
    "${filtered[@]}"
  rc=$?

  if test "$rc" -eq 0 && test -n "$depfile"; then
    dep_cpp_args=()
    dep_expect=
    for arg in "$@"; do
      if test -n "$dep_expect"; then
        dep_cpp_args+=("$dep_expect" "$arg")
        dep_expect=
        continue
      fi
      case "$arg" in
        -I|-D|-U|-include|-isystem|-iquote) dep_expect="$arg" ;;
        -I*|-D*|-U*|-include*|-isystem*|-iquote*) dep_cpp_args+=("$arg") ;;
      esac
    done
    mkdir -p "$(dirname -- "$depfile")"
    "$BUSYBOX_REAL_GCC" \
      --sysroot="$BUSYBOX_MINI_SYSROOT/.." \
      -I"$BUSYBOX_UAPI_COMPAT" "${dep_cpp_args[@]}" \
      -MMD -MF "$depfile" -MT "${dep_target:-$output}" \
      -E "$source" -o /dev/null
    dep_rc=$?
    if test "$dep_rc" -ne 0; then rc=$dep_rc; fi
  fi
  set -e

  status=FAIL
  test "$rc" -eq 0 && status=PASS
  {
    flock 9
    printf '%s\t%s\t%s\t%s\n' "$source" "$status" "$rc" "$output" >&9
  } 9>>"$BUSYBOX_C_RESULTS"
  exit "$rc"
fi

# Reference final link remains GNU until final-link ownership is tested as a
# separate gate. Crucially, capability probes returned above never reach here.
filtered=()
for arg in "$@"; do
  case "$arg" in
    -lm|-lcrypt|-lrt|-lresolv|-lpthread|-lc) ;;
    *) filtered+=("$arg") ;;
  esac
done

set +e
"$BUSYBOX_REAL_GCC" \
  --sysroot="$BUSYBOX_MINI_SYSROOT/.." \
  -nostdlib -static -Wl,-e,_start \
  "$BUSYBOX_MUSL_BUILD/obj/crt/crt1.o" \
  "$BUSYBOX_MUSL_BUILD/obj/crt/crti.o" \
  "${filtered[@]}" \
  -Wl,--start-group "$BUSYBOX_MUSL_BUILD/lib/libc.a" "$BUSYBOX_LIBGCC" -Wl,--end-group \
  "$BUSYBOX_MUSL_BUILD/obj/crt/crtn.o"
link_rc=$?
set -e

# Keep this diagnostic deliberately narrow: the current MiniLD final-link
# blocker is an R_RISCV_JAL to __syscall_ret. Print the exact archive member
# and caller functions that carry such relocations, then show where GNU placed
# those calls in the accepted BusyBox ELF. trylink redirects this wrapper's
# stdout into busybox_unstripped.out, so also append directly to the build log
# that the workflow already preserves as an artifact.
if test "$link_rc" -eq 0 && test -n "$output" && test -s "$output"; then
  tool_prefix="${BUSYBOX_REAL_GCC%gcc}"
  objdump="${tool_prefix}objdump"
  nm="${tool_prefix}nm"
  libc="$BUSYBOX_MUSL_BUILD/lib/libc.a"
  diag_log="$(dirname -- "$BUSYBOX_C_RESULTS")/busybox-mini-build.log"

  {
    echo "BUSYBOX_SYSCALL_RET_DIFF_BEGIN output=$output"
    "$nm" -A "$libc" 2>/dev/null | awk '/[[:space:]]__syscall_ret$/ {print "BUSYBOX_SYSCALL_RET_DEF " $0}'
    "$objdump" -dr "$libc" 2>/dev/null | awk '
      /\):[[:space:]]+file format/ {
        member=$0
        sub(/^.*\(/, "", member)
        sub(/\):[[:space:]].*$/, "", member)
        next
      }
      /^[[:xdigit:]]+[[:space:]]+<[^>]+>:/ {
        caller=$0
        sub(/^[[:xdigit:]]+[[:space:]]+</, "", caller)
        sub(/>:.*/, "", caller)
        next
      }
      /R_RISCV_JAL[[:space:]]+__syscall_ret/ {
        printf "BUSYBOX_SYSCALL_RET_ARCHIVE_JAL member=%s caller=%s reloc=%s\n", member, caller, $0
      }
    '
    "$nm" -an "$output" 2>/dev/null | awk '/[[:space:]]__syscall_ret$/ {print "BUSYBOX_SYSCALL_RET_GNU_DEF " $0}'
    "$objdump" -d "$output" 2>/dev/null | awk '
      /^[[:xdigit:]]+[[:space:]]+<[^>]+>:/ {
        addr=$1
        caller=$0
        sub(/^[[:xdigit:]]+[[:space:]]+</, "", caller)
        sub(/>:.*/, "", caller)
        next
      }
      /jal[^<]*<__syscall_ret>/ {
        printf "BUSYBOX_SYSCALL_RET_GNU_JAL caller=%s caller_addr=0x%s insn=%s\n", caller, addr, $0
      }
    '
    echo "BUSYBOX_SYSCALL_RET_DIFF_END"
  } | tee -a "$diag_log"
fi

exit "$link_rc"

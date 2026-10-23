#!/usr/bin/env python3
"""Pinned original BusyBox 1.36.1 upstream-Kbuild x86-64 MiniC frontier.

Default discovery mode is read-only: the proxy executes GCC on every original
C owner and independently snapshots the same CPP input for MiniC codegen.
Only the explicit BUSYBOX_ALLYES_MIXED_COHORT=1 host-compatible mode can route
precisely selected GNU-assembled MiniC objects into live Kbuild CC owners,
then require real GNU linker + differential executable runtime verification.
Neither mode edits the original BusyBox C files or changes GNU's command flags.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


def compiler_argv():
    return shlex.split(os.environ.get("BUSYBOX_ALLYES_HOST_CC", "cc"))


def call_proxy(args):
    """Preserve GCC behavior; side-channel diagnostics cannot break GCC."""
    gcc_argv = compiler_argv()
    gcc = subprocess.run(gcc_argv + args, check=False)
    # BusyBox trylink uses the same CC wrapper for probing and three
    # distinct link attempts. Record the *actual* reference GCC status and
    # binary state rather than inferring a linker failure from the last
    # printed --verbose map. The report is outside the pristine source.
    if "-o" in args:
        idx = args.index("-o")
        if idx + 1 < len(args):
            output_path = Path(args[idx + 1])
            if output_path.name == "busybox_unstripped":
                report = Path(os.environ["BUSYBOX_ALLYES_REPORT_ROOT"])
                with (report / "gnu-link-attempts.log").open("a", encoding="utf-8") as log:
                    log.write("rc=%d binary_exists=%d binary_bytes=%d "
                              "gc_sections=%d verbose=%d argv_count=%d\n" %
                              (gcc.returncode, int(output_path.is_file()),
                               output_path.stat().st_size if output_path.is_file() else 0,
                               int(any("gc-sections" in arg for arg in args)),
                               int(any("verbose" in arg for arg in args)),
                               len(args)))
    # The only supported replacement path is explicit, exact owner routing
    # from an isolated real Kbuild CC invocation. Kbuild may recompile .o on
    # aggregation due to its .cmd metadata, so copying .o *before* make is
    # not proof of MiniC ownership. Replace just after GCC creates the real
    # target .o and before Kbuild archives/links that object.
    wanted = os.environ.get("BUSYBOX_ALLYES_REPLACE_OWNER")
    map_path = os.environ.get("BUSYBOX_ALLYES_REPLACE_MAP")
    if map_path and gcc.returncode == 0 and "-c" in args:
        manifest = json.loads(Path(map_path).read_text(encoding="utf-8"))
        output = next((args[i + 1] for i, val in enumerate(args[:-1])
                       if val == "-o"), None)
        normalized = str(Path(output).resolve()) if output is not None else None
        if normalized in manifest:
            import shutil
            mini_path = Path(manifest[normalized])
            if not mini_path.is_file():
                raise SystemExit("Kbuild cohort MiniC object missing")
            shutil.copy2(mini_path, output)
            event_dir = Path(os.environ["BUSYBOX_ALLYES_REPORT_ROOT"]) / "combined-events"
            event_dir.mkdir(exist_ok=True)
            event = event_dir / (Path(output).stem + ".replace.txt")
            receipt = "KBUILD_REPLACE_OWNER=%s mini_sha256=%s\n" % (
                output, hashlib.sha256(mini_path.read_bytes()).hexdigest())
            event.write_text(receipt, encoding="utf-8")
            # Two distinct Kbuild directories can both own "main.o".
            # This full-path receipt prevents ambiguous bulk verification.
            unique = hashlib.sha256(normalized.encode("utf-8")).hexdigest()[:20]
            (event_dir / (unique + ".replace.txt")).write_text(
                receipt, encoding="utf-8")
    if wanted and gcc.returncode == 0 and "-c" in args:
        output = next((args[i + 1] for i, val in enumerate(args[:-1])
                       if val == "-o"), None)
        if output is not None and Path(output).resolve() == Path(wanted).resolve():
            mini_path = Path(os.environ["BUSYBOX_ALLYES_REPLACE_MINIC_OBJECT"])
            if not mini_path.is_file():
                raise SystemExit("original-Kbuild replacement MiniC object is missing")
            import shutil
            shutil.copy2(mini_path, output)
            event = Path(os.environ["BUSYBOX_ALLYES_REPLACE_EVENT"])
            event.write_text("KBUILD_REPLACE_OWNER=%s mini_sha256=%s\n" %
                             (output, hashlib.sha256(mini_path.read_bytes()).hexdigest()),
                             encoding="utf-8")
    # When Kbuild relinks a substituted MiniC object cohort, GNU remains
    # the original compiler; never generate a second shadow/report entry.
    if os.environ.get("BUSYBOX_ALLYES_SKIP_SHADOW") == "1":
        return gcc.returncode
    source = next((arg for arg in args if arg.endswith(".c")
                   and not arg.startswith("-")), None)
    if "-c" not in args or source is None:
        return gcc.returncode
    root = Path(os.environ["BUSYBOX_ALLYES_REPORT_ROOT"])
    key = hashlib.sha256((os.getcwd() + "\0" + " ".join(args)).encode()).hexdigest()[:16]
    target = root / "per-tu" / key
    target.mkdir(parents=True, exist_ok=True)
    obj = ""
    for idx, val in enumerate(args[:-1]):
        if val == "-o":
            obj = args[idx + 1]
    info = {"source": source, "object": obj, "gcc": gcc.returncode,
            "cpp": None, "minic": None, "assembler": None}
    # Keep -include/-isystem/-I/-D/-U, source and upstream language flags.
    # Remove the original object output and dependency-writing flags.
    cpp_flags = []
    skip = False
    for i, flag in enumerate(args):
        if skip:
            skip = False
            continue
        if flag in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if flag in ("-c", "-MD", "-MMD", "-MP", "-MG", "-MM", "-M"):
            continue
        if flag.startswith(("-MF", "-MT", "-MQ")):
            continue
        cpp_flags.append(flag)
    preprocessed = target / "input.i"
    asm = target / "output.s"
    obj_mini = target / "mini.o"
    with (target / "cpp.err").open("wb") as e:
        cpp = subprocess.run(gcc_argv + cpp_flags + ["-E", "-P", "-o", str(preprocessed)],
                             stderr=e, stdout=subprocess.DEVNULL, check=False)
    info["cpp"] = cpp.returncode
    if cpp.returncode == 0:
        with (target / "minic.err").open("wb") as e:
            minic_args = [os.environ["BUSYBOX_ALLYES_MINIC_CC"],
                          "--target=" + os.environ.get("BUSYBOX_ALLYES_MINIC_TARGET",
                                                       "x86_64-linux")]
            if os.environ.get("BUSYBOX_ALLYES_MINIC_PIC") == "1":
                minic_args.append("-fPIE")
            minic_args.extend(["-S", str(preprocessed), "-o", str(asm)])
            mini = subprocess.run(
                minic_args,
                stdout=subprocess.DEVNULL, stderr=e, check=False,
                env=({**os.environ, "MINIC_X86_CALL_TRACE": "1"}
                     if os.environ.get("BUSYBOX_ALLYES_MINIC_TARGET",
                                       "x86_64-linux") == "x86_64-linux"
                     else os.environ))
        info["minic"] = mini.returncode
        # Two remaining x86 families (cpuid inline asm and SysV va_list)
        # need a bounded secondary run with Core diagnostics. This does
        # not edit original BusyBox and only reruns an actually failed TU.
        if mini.returncode and os.environ.get(
                "BUSYBOX_ALLYES_MINIC_TARGET", "x86_64-linux") == "x86_64-linux" \
                and source in ("libbb/hash_md5_sha.c", "libbb/verror_msg.c"):
            with (target / "trace.err").open("wb") as e:
                try:
                    subprocess.run(
                        [os.environ["BUSYBOX_ALLYES_MINIC_CC"], "--target=x86_64-linux",
                         "-S", str(preprocessed), "-o", str(asm)],
                        stdout=subprocess.DEVNULL, stderr=e, check=False,
                        timeout=40,
                        env={**os.environ, "MINIC_M1_TRACE": "1",
                             "MINIC_ASM_BATCH_TRACE": "1",
                             "MINIC_BACKEND_CENSUS": "1"})
                except subprocess.TimeoutExpired:
                    e.write(b"BUSYBOX_ALLYES_X86_TRACE_TIMEOUT\\n")
        # Trace only representative *failed* upstream source owners.
        # A small class-level replay avoids a second 581-TU scan while
        # producing the actual Core instruction kind / rejecting guard.
        trace_owners = ("coreutils/od.c", "libbb/xconnect.c",
                        "miscutils/devfsd.c", "miscutils/devmem.c",
                        "miscutils/hdparm.c", "networking/ifconfig.c",
                        "procps/top.c")
        if (mini.returncode and os.environ.get("BUSYBOX_ALLYES_MINIC_TARGET",
                                                  "x86_64-linux") != "x86_64-linux"
                and source in trace_owners):
            with (target / "trace.err").open("wb") as e:
                try:
                    subprocess.run(
                        [os.environ["BUSYBOX_ALLYES_MINIC_CC"],
                         "--target=" + os.environ["BUSYBOX_ALLYES_MINIC_TARGET"],
                         "-S", str(preprocessed), "-o", str(asm)],
                        stdout=subprocess.DEVNULL, stderr=e, check=False,
                        timeout=40, env={**os.environ, "MINIC_M1_TRACE": "1",
                                         "MINIC_BACKEND_CENSUS": "1"})
                except subprocess.TimeoutExpired:
                    e.write(b"BUSYBOX_ALLYES_CORE_TRACE_TIMEOUT\n")
        if mini.returncode == 0:
            with (target / "as.err").open("wb") as e:
                ass = subprocess.run(gcc_argv + ["-c", "-x", "assembler",
                                                str(asm), "-o", str(obj_mini)],
                                     stdout=subprocess.DEVNULL, stderr=e, check=False)
            info["assembler"] = ass.returncode
    (target / "result.json").write_text(json.dumps(info, sort_keys=True), encoding="utf-8")
    return gcc.returncode


def print_frontier(report_root, gcc_success, enabled):
    results = []
    for path in (report_root / "per-tu").glob("*/result.json"):
        results.append((path, json.loads(path.read_text(encoding="utf-8"))))
    results.sort(key=lambda x: (x[1]["source"], x[1]["object"]))
    stats = {"minic_object_pass": 0, "gcc_fail": 0, "cpp_fail": 0,
             "minic_fail": 0, "assembler_fail": 0}
    for path, info in results:
        if info["gcc"] != 0:
            stats["gcc_fail"] += 1
        if info["cpp"] != 0:
            stats["cpp_fail"] += 1
            stage = "CPP"
        elif info["minic"] != 0:
            stats["minic_fail"] += 1
            stage = "MiniC"
        elif info["assembler"] != 0:
            stats["assembler_fail"] += 1
            stage = "GNU_AS"
        else:
            stats["minic_object_pass"] += 1
            stage = "PASS"
        print("BUSYBOX_ALLYES_TU source=%s object=%s stage=%s gcc=%s" %
              (info["source"], info["object"], stage, info["gcc"]))
        if stage != "PASS":
            fname = {"CPP": "cpp.err", "MiniC": "minic.err",
                     "GNU_AS": "as.err"}[stage]
            error = (path.parent / fname).read_text(encoding="utf-8", errors="replace")
            # GNU AS uses capitalized "Error:" and starts stderr with
            # "Assembler messages:". Report the actual first failing line,
            # not that uninformative header, on every architecture.
            useful = [s.strip() for s in error.splitlines()
                      if "error:" in s.lower() or "unsupported" in s.lower()]
            if not useful:
                useful = [s.strip() for s in error.splitlines() if s.strip()]
            print("BUSYBOX_ALLYES_BLOCKER source=%s kind=%s detail=%s" %
                  (info["source"], stage, (useful[0] if useful else "no diagnostic")[:240]))
            if stage == "MiniC" and info["source"] in (
                    "libbb/xconnect.c", "libbb/vfork_daemon_rexec.c",
                    "selinux/chcon.c", "selinux/setfiles.c",
                    "util-linux/mkswap.c", "networking/tls_sp_c32.c"):
                # Original BusyBox source remains pristine. This context is
                # from the exact GNU-preprocessed file fed to MiniC. It
                # distinguishes type/attribute frontend defects from the
                # upstream C line and avoids blind source-specific fixes.
                # Upstream GNU CPP is the oracle. Report the exact
                # preprocessed expression at the frontend rejection site;
                # raw xconnect.c line numbers are NOT the same after the
                # target sysroot headers are expanded.
                match = re.search(r"input\.i:(\d+):(\d+):\s*error:", error)
                if match:
                    source_lines = (path.parent / "input.i").read_text(
                        errors="replace").splitlines()
                    at = int(match.group(1))
                    preview = " | ".join(
                        "%d:%s" % (k + 1, source_lines[k].strip())
                        for k in range(max(0, at - 3),
                                       min(len(source_lines), at + 2))
                    )
                    print("BUSYBOX_ALLYES_CPP_CONTEXT source=%s line=%d text=%s" %
                          (info["source"], at, preview[:1100]), flush=True)
            if stage == "MiniC":
                trace_path = path.parent / "trace.err"
                if trace_path.is_file():
                    trace = trace_path.read_text(errors="replace")
                    captures = [line.strip() for line in trace.splitlines()
                                if line.startswith(("MINIC_BACKEND_REJECT",
                                                    "MINIC_M1_FRONTIER",
                                                    "MINIC_M4_GLOBAL_FRONTIER",
                                                    "MINIC_M8_OPCODE_REJECT",
                                                    "MINIC_SQLITE_CONVERSION_LIMIT",
                                                    "MINIC_BACKEND_PREFLIGHT",
                                                    "MINIC_BACKEND_CENSUS",
                                                    "MINIC_CALL_RESULT_REJECT",
                                                    "MINIC_BACKEND_CALL_GATE",
                                                    "MINIC_M8_FRAME_REJECT",
                                                    "CORE_EXPR_STMT_DETAIL",
                                                    "CORE_ASM_DETAIL",
                                                    "CORE_LOWER_"))]
                    for detail in captures[:6]:
                        print("BUSYBOX_ALLYES_CORE_REJECT source=%s detail=%s" %
                              (info["source"], detail[:550]), flush=True)
                gates = [line.strip() for line in error.splitlines()
                         if line.startswith("X86_CALL_GATE ")]
                arguments = [line.strip() for line in error.splitlines()
                             if line.startswith("X86_CALL_ARG ")]
                if gates:
                    print("BUSYBOX_ALLYES_CALL_GATE source=%s detail=%s" %
                          (info["source"], gates[-1][:420]))
                    for arg in arguments[:16]:
                        print("BUSYBOX_ALLYES_CALL_ARG source=%s detail=%s" %
                              (info["source"], arg[:340]))

    total = len(results)
    print("BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=%d attempted_C_TUs=%d "
          "mini_objects=%d cpp_fail=%d minic_fail=%d asm_fail=%d gcc_fail=%d "
          "gcc_full_build=%s full_MiniC_busybox=NOT_TESTED" %
          (enabled, total, stats["minic_object_pass"], stats["cpp_fail"],
           stats["minic_fail"], stats["assembler_fail"], stats["gcc_fail"],
           "PASS" if gcc_success else "FAIL"))
    (report_root / "summary.json").write_text(
        json.dumps({"enabled_symbols": enabled, "kbuild_completed": gcc_success,
                    "attempted_C_TUs": total, **stats}, indent=2, sort_keys=True),
        encoding="utf-8")
    return total


def controller(args):
    root = Path(args.source).resolve()
    out = Path(args.report).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if (out / "per-tu").exists():
        raise SystemExit("refuse stale scan directory: " + str(out / "per-tu"))
    make_env = os.environ.copy()
    for k in ("MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS", "GNUMAKEFLAGS", "CFLAGS"):
        make_env.pop(k, None)
    # Kconfig is the authoritative way to request all possible options.
    with (out / "config.out").open("wb") as stdout, (out / "config.err").open("wb") as stderr:
        rc = subprocess.run(["make", "-j2", "allyesconfig", "CC=" + args.host_cc],
                            cwd=root, env=make_env, stdout=stdout, stderr=stderr,
                            check=False).returncode
    if rc:
        print("BUSYBOX_ALLYES_CONFIG=FAIL stage=original-allyesconfig")
        print((out / "config.err").read_text(errors="replace")[-2500:])
        return 1
    config_path = root / ".config"
    config = config_path.read_text(errors="replace")
    # Optional maximal *host-link-compatible* original BusyBox config.
    # Default remains the complete upstream applet discovery; only host ABI
    # obsolescence is excluded in this explicit mode (never patch applet C).
    link_compatible = os.environ.get("BUSYBOX_ALLYES_LINK_COMPATIBLE") == "1"
    disabled_host_options = ("WERROR", "TC", "FEATURE_INETD_RPC",
                             "FEATURE_MOUNT_NFS", "STATIC",
                             "DEBUG_SANITIZE") if link_compatible else ("WERROR",)
    # Genuine cross sysroots need not contain the host's libselinux headers
    # or archive. Disable that single original upstream Kconfig option,
    # NOT its C sources, instead of presenting 957 enabled options as a
    # valid target build when applets/applets.c cannot even preprocess.
    if link_compatible and (
            os.environ.get("BUSYBOX_ALLYES_MINIC_TARGET",
                           "x86_64-linux") != "x86_64-linux" or
            os.environ.get("BUSYBOX_ALLYES_MUSL_MATRIX") == "1"):
        # Upstream libbb/common_bufsiz.c explicitly defaults this
        # optional page-tail optimization OFF, and documents disabling it
        # for linker/post-link _end failures. The allyesconfig-only
        # scripts/generate_BUFSIZ.sh --post probes host NM/CC on a target
        # ELF and can fail *after* GNU successfully links BusyBox. Do not
        # patch original BusyBox scripts or misclassify this as MiniC ABI.
        disabled_host_options += ("SELINUX", "PAM", "FEATURE_USE_BSS_TAIL")
    changed = []
    for symbol in disabled_host_options:
        old_value = "CONFIG_" + symbol + "=y"
        not_set = "# CONFIG_" + symbol + " is not set"
        if old_value in config:
            config = config.replace(old_value, not_set)
            changed.append(symbol)
        elif link_compatible and not_set not in config:
            print("BUSYBOX_ALLYES_CONFIG=FAIL missing-symbol=%s" % symbol)
            return 1
    if changed:
        config_path.write_text(config, encoding="utf-8")
        with (out / "oldconfig.out").open("wb") as stdout, (out / "oldconfig.err").open("wb") as stderr:
            rc = subprocess.run(["make", "oldconfig", "CC=" + args.host_cc],
                                cwd=root, env=make_env, input=b"\n" * 32,
                                stdout=stdout, stderr=stderr, check=False).returncode
        if rc:
            print("BUSYBOX_ALLYES_CONFIG=FAIL stage=oldconfig-host-policy")
            return 1
        config = config_path.read_text(errors="replace")
        for symbol in changed:
            if "CONFIG_" + symbol + "=y" in config:
                print("BUSYBOX_ALLYES_CONFIG=FAIL failed-to-disable=%s" % symbol)
                return 1
        print("BUSYBOX_ALLYES_HOST_EXCLUSIONS=PASS disabled=%s original_sources=unchanged" %
              ",".join(changed), flush=True)
    if link_compatible:
        print("BUSYBOX_ALLYES_HOST_LINK_POLICY=TRY excluded=%s" %
              ",".join(disabled_host_options), flush=True)
    enabled = len(re.findall(r"^CONFIG_[A-Za-z0-9_]+=y$", config, flags=re.M))
    print("BUSYBOX_ALLYES_CONFIG=PASS mode=original-allyesconfig enabled_symbols=%d" %
          enabled, flush=True)
    make_env["BUSYBOX_ALLYES_MINIC_CC"] = str(Path(args.minic).resolve())
    make_env["BUSYBOX_ALLYES_HOST_CC"] = args.host_cc
    make_env["BUSYBOX_ALLYES_REPORT_ROOT"] = str(out)
    proxy = "python3 " + shlex.quote(str(Path(__file__).resolve())) + " --proxy-cc"
    with (out / "build.out").open("wb") as stdout, (out / "build.err").open("wb") as stderr:
        rc = subprocess.run(["make", "-k", "-j4", "CC=" + proxy, "busybox"],
                            cwd=root, env=make_env, stdout=stdout, stderr=stderr,
                            check=False).returncode
    if rc != 0:
        # RV64's existing Kbuild wrapper distinguishes C producers from
        # build-system control traffic. Reuse the exact original Kbuild
        # dependency graph; after the comprehensive -k shadow census a
        # sequential GNU-only pass isolates the FIRST real failing target,
        # without repeating all MiniC compiler invocations or forging a
        # successful full build.
        retry_env = dict(make_env)
        retry_env["BUSYBOX_ALLYES_SKIP_SHADOW"] = "1"
        with (out / "gnu-sequential.out").open("wb") as stdout, \
             (out / "gnu-sequential.err").open("wb") as stderr:
            retry_rc = subprocess.run(
                ["make", "-j1", "CC=" + proxy, "busybox"],
                cwd=root, env=retry_env, stdout=stdout, stderr=stderr,
                check=False).returncode
        print("BUSYBOX_ALLYES_GCC_SEQUENTIAL=%s initial_parallel_rc=%d sequential_rc=%d" %
              ("PASS" if retry_rc == 0 else "BLOCKED", rc, retry_rc),
              flush=True)
        if retry_rc == 0:
            rc = 0
        else:
            # Avoid searching for the string 'error:' in the *source
            # snippets* of a GCC warning such as bb_error_msg("read error:").
            # Source snippets were previously mistaken for linker errors.
            pattern = re.compile(
                r"^(?:[^ ]+:[0-9]+(?::[0-9]+)?: )?(?:fatal )?error:"
                r"|undefined reference|^collect2:|^/usr/bin/ld:"
                r"|^make(?:\\[[0-9]+\\])?: \\*\\*\\*"
                r"|No rule to make target|cannot find -l"
            )
            for filename in ("gnu-sequential.err", "gnu-sequential.out",
                             "build.err", "build.out"):
                text = (out / filename).read_text(errors="replace")
                lines = [line.strip() for line in text.splitlines()
                         if pattern.search(line)]
                print("BUSYBOX_ALLYES_GCC_REAL_ERRORS file=%s matches=%d first=%s" %
                      (filename, len(lines),
                       " | ".join(lines[:16])[:3200]), flush=True)
                if filename.startswith("gnu-sequential"):
                    print("BUSYBOX_ALLYES_GCC_STOP_CONTEXT file=%s lines=%s" %
                          (filename,
                           " | ".join(text.splitlines()[-32:])[:2500]),
                          flush=True)
            linker_log = root / "busybox_unstripped.out"
            if linker_log.is_file():
                ls = linker_log.read_text(errors="replace")
                whole_lines = ls.splitlines()
                root_errors = [(i + 1, ln.strip()) for i, ln in enumerate(whole_lines)
                               if re.search(
                                 r"undefined reference|collect2:|error:|fatal:|"
                                 r"relocation (?:.*)out of range|cannot find|"
                                 r"No such file or directory|file not recognized|"
                                 r"^/usr/[^ ]*/ld:|^ld: |^FAIL|^Failed",
                                 ln)]
                print("BUSYBOX_ALLYES_GCC_FINAL_LINK_CAUSE matches=%d lines=%s" %
                      (len(root_errors),
                       " | ".join("line%d:%s" % item for item in
                                  root_errors[:25])[:5800]), flush=True)
                print("BUSYBOX_ALLYES_GCC_FINAL_LINK_FIRST_LINES=" +
                      " | ".join(whole_lines[:25])[:3300], flush=True)
                print("BUSYBOX_ALLYES_GCC_FINAL_LINK_LAST_LINES=" +
                      " | ".join(whole_lines[-18:])[:2300], flush=True)
            print("BUSYBOX_ALLYES_GCC_LINK_FEATURES=" +
                  " ".join(line for line in config.splitlines()
                           if line.startswith(("CONFIG_BUILD_LIBBUSYBOX=",
                                               "CONFIG_FEATURE_INDIVIDUAL=",
                                               "CONFIG_FEATURE_SHARED_BUSYBOX=",
                                               "CONFIG_PIE=", "CONFIG_EXTRA_LDLIBS=",
                                               "CONFIG_USE_BB_CRYPT="))),
                  flush=True)
    attempted_link_log = out / "gnu-link-attempts.log"
    if attempted_link_log.is_file():
        print("BUSYBOX_ALLYES_GCC_LINK_ATTEMPTS=" +
              " | ".join(attempted_link_log.read_text().splitlines())[:2800],
              flush=True)
    print("BUSYBOX_ALLYES_GCC_KBUILD=%s original_Kconfig=1 original_sources=1" %
          ("PASS" if rc == 0 else "BLOCKED"), flush=True)
    if link_compatible:
        busybox = root / "busybox"
        if rc == 0 and busybox.is_file():
            tests = {
                "sed": ["sed", "-n", "2p"],
                "awk": ["awk", "{print $1}"],
                "grep": ["grep", "-F", "beta"],
                "sort": ["sort"],
                "sh": ["sh", "-c", "printf '%s\n' sh-ok"],
                "find": ["find", str(out), "-maxdepth", "0", "-type", "d"],
            }
            expected = {"sed": b"beta\n", "awk": b"alpha\nbeta\n",
                        "grep": b"beta\n", "sort": b"alpha\nbeta\n",
                        "sh": b"sh-ok\n",
                        "find": (str(out) + "\n").encode()}
            successes = []
            for applet, command in tests.items():
                data = b"alpha\nbeta\n" if applet not in ("sh", "find") else b""
                qemu = os.environ.get("BUSYBOX_ALLYES_QEMU")
                sysroot = os.environ.get("BUSYBOX_ALLYES_SYSROOT", "")
                launch = ([qemu, "-L", sysroot] if qemu else [])
                run = subprocess.run(launch + [str(busybox)] + command, input=data,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     check=False)
                passed = run.returncode == 0 and run.stdout == expected[applet]
                print("BUSYBOX_ALLYES_GCC_RUNTIME=%s applet=%s" %
                      ("PASS" if passed else "FAIL", applet), flush=True)
                if not passed:
                    print("BUSYBOX_ALLYES_GCC_RUNTIME_DETAIL applet=%s exit=%d stdout=%r stderr=%r expected=%r" %
                          (applet, run.returncode, run.stdout[:160],
                           run.stderr[:160], expected[applet][:160]), flush=True)
                successes.append(passed)
            if all(successes):
                print("BUSYBOX_ALLYES_GCC_RUNTIME_ORACLE=PASS applets=%d original_full_Kbuild=1" %
                      len(tests), flush=True)
            else:
                print("BUSYBOX_ALLYES_GCC_RUNTIME_ORACLE=FAIL", flush=True)
        else:
            print("BUSYBOX_ALLYES_GCC_RUNTIME_ORACLE=BLOCKED original_GNU_build=%s" %
                  ("PASS" if rc == 0 else "FAIL"), flush=True)
    attempts = print_frontier(out, rc == 0, enabled)
    if link_compatible and os.environ.get("BUSYBOX_ALLYES_MIXED_BULK") == "1":
        if rc != 0 or not (root / "busybox").is_file():
            print("BUSYBOX_MIXED_BULK=BLOCKED stage=original-GNU-baseline")
            return 1
        bulk_script = Path(__file__).resolve().with_name("mixed_bulk_kbuild.py")
        bulk_rc = subprocess.run(
            [sys.executable, str(bulk_script), "--source", str(root),
             "--report", str(out), "--proxy", proxy, "--host-cc", args.host_cc],
            env=make_env, check=False).returncode
        if bulk_rc:
            print("BUSYBOX_MIXED_BULK=FAIL stage=bulk-cohort")
            return 1
    if link_compatible and os.environ.get("BUSYBOX_ALLYES_MIXED_COHORT") == "1":
        if rc != 0 or not (root / "busybox").is_file():
            print("BUSYBOX_MIXED_COMPLEX=BLOCKED stage=original-GNU-build")
            return 1
        mixed_script = Path(__file__).resolve().with_name("mixed_original_kbuild.py")
        verdict = subprocess.run(
            [sys.executable, str(mixed_script), "--source", str(root),
             "--report", str(out), "--proxy", proxy, "--host-cc", args.host_cc],
            env=make_env, check=False)
        if verdict.returncode != 0:
            print("BUSYBOX_MIXED_COMPLEX=FAIL stage=cohort-validation")
            return 1
    if attempts == 0:
        print("BUSYBOX_ALLYES_SCAN=FAIL no_real_Kbuild_C_compile_invocations")
        return 1
    print("BUSYBOX_ALLYES_SCAN=PASS diagnostic_only=1 no_MiniC_objects_inserted=1")
    # A red original GCC build or many MiniC failures are meaningful frontier
    # results, not a green full BusyBox certification.
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--proxy-cc":
        return call_proxy(sys.argv[2:])
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--minic", required=True)
    parser.add_argument("--host-cc", default="cc")
    return controller(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())

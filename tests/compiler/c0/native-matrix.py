#!/usr/bin/env python3
"""Execute named compiler regression matrices with real GNU and MiniC oracles.

Case data describes C inputs, dialect/ABI flags, target sets, exit values,
assembly contracts and invalid-source diagnostics. It contains no shell code.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
DATA = json.loads(Path(__file__).with_name("native-matrix.json").read_text())
TARGETS = ["x86_64", "aarch64", "loongarch64", "riscv64", "riscv32"]

def flags(value):
    return shlex.split(value or "")

def source(name):
    if name.startswith("tests/"):
        path = ROOT / name
    else:
        parent = "tests/compiler/" if name.startswith("audit/") else "tests/compiler/c0/"
        path = ROOT / (parent + name + ("" if name.endswith((".c", ".i")) else ".c"))
    if not path.is_file():
        raise ValueError(f"missing regression input {path}")
    return str(path)

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def execute(argv, env, expect=0, label="command"):
    p = subprocess.run([str(a) for a in argv], cwd=ROOT, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    success = p.returncode == expect if expect != "reject" else p.returncode > 0
    if not success or (expect == "reject" and
                       any(x in p.stderr for x in (b"AddressSanitizer", b"heap-use-after-free", b"runtime error:", b"Segmentation fault"))):
        sys.stderr.buffer.write(p.stdout + p.stderr)
        raise RuntimeError(f"{label}: {p.returncode}, expected {expect}: {shlex.join([str(a) for a in argv])}")
    return p

def matching(content, mode, pattern):
    p = subprocess.run(["grep", "-" + mode + "q", "--", pattern], input=content)
    require(p.returncode in (0, 1), f"invalid grep pattern {pattern!r}")
    return p.returncode == 0

def check_all(checks, artifacts, targets, counts=(), cfg=()):
    for target, sense, mode, pattern, *section in checks:
        for t in targets if target == "*" else [target]:
            require(t in artifacts, f"missing check artifact {t}")
            data = artifacts[t]
            if section:
                start = (section[0] + ":").encode()
                end = (".size " + section[0]).encode()
                lines = data.splitlines(keepends=True)
                first = next((i for i, line in enumerate(lines) if line.strip() == start), -1)
                require(first >= 0, f"{t}: missing function {section[0]}")
                last = next((i for i in range(first + 1, len(lines))
                             if lines[i].strip().startswith(end)), len(lines))
                data = b"".join(lines[first:last + 1])
            require(matching(data, mode, pattern) == (sense == "present"),
                    f"{t}: {sense} {pattern!r} failed")

    for target, mode, pattern, minimum, maximum in counts:
        data = artifacts[target]
        proc = subprocess.run(["grep", "-" + mode + "c", "--", pattern], input=data, stdout=subprocess.PIPE)
        require(proc.returncode in (0, 1), f"invalid count pattern {pattern!r}")
        count = int(proc.stdout)
        require(count >= minimum and (maximum is None or count <= maximum),
                f"{target}: count {count} outside {minimum}..{maximum}: {pattern}")
    for target, function, minimum in cfg:
        require(function.replace("_", "").isalnum(), "invalid CFG function")
        pattern = r"^\.Lminic_mir_" + function + r"_[0-9]+:$"
        check_all([[target, "present", "E", r"^[[:space:]]+ret$"]], artifacts, targets,
                  [[target, "E", pattern, minimum, None]])

def compiler(mode, supplied):
    if mode == "legacy":
        return os.environ.get("MINIC", str(ROOT / "build/debug/bin/minic"))
    if mode == "arg-first" and supplied:
        return supplied
    if mode == "arg":
        require(supplied is not None, "MiniC compiler path argument required")
        return supplied
    if mode == "MINIC":
        return os.environ["MINIC"]
    if mode == "required":
        return os.environ["MINIC_CC"]
    default = ROOT / "build" / ("debug" if mode == "debug" else "release") / "bin/minic-cc"
    return os.environ.get("MINIC_CC") or (supplied if mode == "optional-arg" else None) or str(default)

def host(mode, supplied=None):
    if mode == "arg":
        return supplied or "cc"
    return {"cc": "cc", "gcc": "gcc",
            "CC": os.environ.get("CC", "cc"),
            "HOST_CC": os.environ.get("HOST_CC", "cc"),
            "HOST_CC_CC": os.environ.get("HOST_CC", os.environ.get("CC", "cc")),
            "HOST_CC_GCC": os.environ.get("HOST_CC", "gcc")}[mode]

def settings(group, row):
    return {**group, **row}

def environment(config):
    env = os.environ.copy()
    if "asan" in config:
        env.setdefault("ASAN_OPTIONS", config["asan"])
    if "asan_force" in config:
        env["ASAN_OPTIONS"] = config["asan_force"]
    return env

def preprocess(config, cc, src, out, env):
    if config.get("direct_input"):
        out.write_bytes(Path(src).read_bytes())
    else:
        execute([cc, *flags(config.get("pre_flags", "-std=gnu11 -E -P -x c")),
                 *flags(config.get("defines")), src, "-o", out], env, label="GNU preprocessing")
    for old, new in config.get("mutations", []):
        raw = out.read_bytes()
        require(old.encode() in raw, f"mutation missing: {old!r}")
        out.write_bytes(raw.replace(old.encode(), new.encode()))

def minic_compile(config, mini, input_path, output_path, target, env, reject=False):
    output_path.unlink(missing_ok=True)
    env = dict(env, **config.get("mini_env", {}))
    cmd = [mini]
    if not config.get("omit_target_option"):
        cmd.append("--target=" + target + "-linux")
    p = execute([*cmd, *flags(config.get("mini_flags")),
                 *flags(config.get("target_flags", {}).get(target)),
                 "-S", input_path, "-o", output_path], env,
                expect="reject" if reject else 0, label="MiniC " + target)
    if reject:
        require(not output_path.exists() or not output_path.stat().st_size,
                f"{target}: rejected input left assembly")
    else:
        require(output_path.is_file() and output_path.stat().st_size > 0,
                f"{target}: empty assembly")
    return p

def positive(config, cc, mini, work):
    env = environment(config)
    src = source(config["source"])
    input_path = work / "input.i"
    preprocess(config, cc, src, input_path, env)
    if config.get("direct_input") and not config.get("mutations"):
        input_path = Path(src)
    if "syntax" in config:
        execute([cc, *flags(config["syntax"]), *flags(config.get("defines")), src], env)
    if "gnu_object_flags" in config:
        gnu_input = input_path if config.get("gnu_object_input") == "preprocessed" else src
        execute([cc, *flags(config["gnu_object_flags"]), gnu_input,
                 "-o", work / "gnu-source.o"], env, label="GNU source object")
    support = [source(s) for s in config.get("support", [])]
    targets = config.get("targets", TARGETS)
    artifacts = {}
    if config.get("gnu", True):
        gnu = work / "gnu"
        kind = config.get("gnu_kind", "source")
        gnu_flags = flags(config.get("gnu_flags", "-std=gnu11 -O0 -no-pie"))
        if kind == "asm":
            gnu_s, gnu_o = work / "gnu.s", work / "gnu.o"
            execute([cc, *gnu_flags, input_path, "-o", gnu_s], env, label="GNU assembly")
            execute([cc, "-c", "-x", "assembler", gnu_s, "-o", gnu_o], env)
            artifacts["gnu"] = gnu_s.read_bytes()
            artifacts["gnu_nm"] = execute(["nm", "-u", gnu_o], env).stdout
            execute([cc, *flags(config.get("link_flags", "-no-pie")), gnu_o, "-o", gnu], env)
        else:
            inp = input_path if kind == "preprocessed" else src
            define_flags = [] if kind == "preprocessed" else flags(config.get("defines"))
            gnu_build = execute([cc, *gnu_flags, *define_flags, inp, *support, "-o", gnu], env,
                                label="GNU native build")
            if config.get("relay_gnu_build_stderr"):
                sys.stderr.buffer.write(gnu_build.stderr)
        g = execute([gnu], env, expect=config.get("exit", 0), label="GNU runtime")
        if config.get("relay_runtime_output", True):
            sys.stdout.buffer.write(g.stdout)
            sys.stderr.buffer.write(g.stderr)
    for target in targets:
        out = work / (target + "-linux.s")
        p = minic_compile(config, mini, input_path, out, target, env)
        artifacts[target] = out.read_bytes()
        artifacts[target + "_log"] = p.stderr
        if config.get("relay_compile_stderr"):
            sys.stderr.buffer.write(p.stderr)
    if config.get("native", True):
        require("x86_64" in targets, "native test must compile x86_64")
        mini_s = work / "x86_64-linux.s"
        mini_exe = work / "minic"
        link = flags(config.get("link_flags", "-no-pie"))
        if config.get("object"):
            obj = work / "minic.o"
            execute([cc, "-c", "-x", "assembler", mini_s, "-o", obj], env)
            artifacts["nm"] = execute(["nm", "-u", obj], env).stdout
            execute([cc, *link, obj, *support, "-o", mini_exe], env)
        else:
            execute([cc, *link, mini_s, *support, "-o", mini_exe], env)
        p = execute([mini_exe], env, expect=config.get("exit", 0), label="MiniC native runtime")
        artifacts["stdout"] = p.stdout
        if config.get("relay_runtime_output", True):
            sys.stdout.buffer.write(p.stdout)
            sys.stderr.buffer.write(p.stderr)
        if config.get("compare"):
            require(config.get("gnu", True) and p.stdout == g.stdout and p.stderr == g.stderr,
                    "GNU and MiniC native outputs diverge")
    check_all(config.get("checks", []), artifacts, targets, config.get("counts", []), config.get("cfg", []))

def negative(config, cc, mini, work):
    env = environment(config)
    src = source(config["source"])
    ip = work / "negative.i"
    preprocess(config, cc, src, ip, env)
    if config.get("direct_input") and not config.get("mutations"):
        ip = Path(src)
    for verdict in ("gnu_accept", "gnu_reject"):
        if verdict not in config:
            continue
        opts = flags(config[verdict])
        inp = ip if config.get("gnu_input") == "preprocessed" else src
        defines = [] if inp == ip else flags(config.get("defines"))
        output = ["-o", work / "invalid.gnu.o"] if "-c" in opts else []
        execute([cc, *opts, *defines, inp, *output], env,
                expect="reject" if verdict == "gnu_reject" else 0, label=verdict)
    for target in config.get("targets", TARGETS):
        p = minic_compile(config, mini, ip, work / (target + ".s"), target, env, reject=True)
        if "diagnostic" in config:
            require(config["diagnostic"].encode() in p.stderr,
                    f"{target}: unrelated diagnostic: {p.stderr.decode(errors='replace')}")
        check_all(config.get("checks", []), {"stderr": p.stderr}, ["stderr"])

UNIT_FLAGS = {
    "basic": "-std=c11 -Wall -Wextra -Werror",
    "pedantic": "-std=c11 -Wall -Wextra -Wpedantic -Werror",
    "strict": "-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror",
    "typed": "-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror",
}
CORE_IR = ["src/core/core_ir.c", "src/compiler/c_lower/core_ir_source.c",
           "src/core/core_type.c", "src/core/core_ir_verify.c", "src/frontend/type.c"]

def host_units(group, cc, work):
    env = environment(group)
    if group.get("language_boundary"):
        execute([ROOT / "tests/core/check-core-language-boundary.sh", cc], env)
    for i, unit in enumerate(group["units"]):
        opts = settings(group, unit)
        sources = unit["sources"] + (CORE_IR if unit.get("core_ir") else [])
        for mode in opts.get("modes", ["native"]):
            extra = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if mode == "asan" else flags(opts.get("optimization", "-O2"))
            gc = flags("-ffunction-sections -fdata-sections -Wl,--gc-sections") if opts.get("gc") else []
            includes = ["-I" + str(ROOT / name) for name in opts.get("includes", ["src", "include"])]
            exe = work / f"unit-{i}-{mode}"
            execute([cc, *flags(UNIT_FLAGS[opts.get("warnings", "basic")]), *extra, *gc,
                     *includes, *(ROOT / s for s in sources), "-o", exe], env, label="host unit build")
            for args in unit.get("args", [[]]):
                result = execute([exe, *args], dict(env, ASAN_OPTIONS="detect_leaks=0"), label="host unit runtime")
                sys.stdout.buffer.write(result.stdout)
                sys.stderr.buffer.write(result.stderr)

def run(name, supplied):
    group = DATA[name]
    mini = None if "units" in group else compiler(group.get("mini", "release"), supplied)
    cc = host(group.get("host", "HOST_CC"), supplied)
    with tempfile.TemporaryDirectory(prefix=name + "-") as tmp:
        work = Path(tmp)
        if "work" in group:
            default = ROOT / "build" / group.get("work_default", "debug")
            if group.get("work_root"):
                base = Path(os.environ.get("BUILD_DIR", ROOT / "build" / group["work"]))
            else:
                base = Path(os.environ.get("BUILD_DIR", default)) / group["work"]
            base.mkdir(parents=True, exist_ok=True)
            work = base.resolve()
        if "units" in group:
            host_units(group, cc, work)
        for index, row in enumerate(group.get("positive", [])):
            dest = work if index == 0 else work / f"positive-{index}"
            dest.mkdir(parents=True, exist_ok=True)
            positive(settings(group, row), cc, mini, dest)
            if row.get("marker"):
                print(row["marker"], flush=True)
        for index, row in enumerate(group.get("negative", [])):
            dest = work / f"negative-{index}"
            dest.mkdir(parents=True, exist_ok=True)
            negative(settings(group, row), cc, mini, dest)
            if row.get("marker"):
                print(row["marker"], flush=True)
        if group["marker"]:
            print(group["marker"], flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", nargs="?")
    parser.add_argument("compiler", nargs="?")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.list:
        print("\n".join(sorted(DATA)))
    elif args.case in DATA:
        try:
            run(args.case, args.compiler)
        except (RuntimeError, ValueError, KeyError, OSError, subprocess.TimeoutExpired) as error:
            print(f"{args.case}: FAIL {error}", file=sys.stderr)
            return 1
    else:
        parser.error("unknown native case; use --list")
    return 0

if __name__ == "__main__":
    sys.exit(main())

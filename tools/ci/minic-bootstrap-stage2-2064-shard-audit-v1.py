#!/usr/bin/env python3
"""Fail-closed 2064 Kbuild per-shard Stage2 compiler dispatch provenance.

Check *every* canonical C owner has a successful Kbuild object AND a QEMU
Stage2 invocation for that exact .minic-stage2.s output. The existing
cohort-bisect pipeline independently verifies ELF ABI, GNU golden .cmd and
actual object SHA. This audit prevents silent Stage0/GCC fallback.
"""
import argparse
from collections import Counter
from pathlib import Path, PurePosixPath
from tempfile import TemporaryDirectory
import subprocess
import sys


def read_owners(path):
    members=[x.strip() for x in Path(path).read_text().splitlines()
             if x.strip() and not x.lstrip().startswith("#")]
    if not 280 <= len(members) <= 310 or len(set(members))!=len(members):
        raise RuntimeError("not a complete seven-way 2064 C partition")
    for owner in members:
        p=PurePosixPath(owner)
        if p.is_absolute() or ".." in p.parts or not owner.endswith(".o"):
            raise RuntimeError("bad owner path "+owner)
    return members


def self_test():
    """Synthetic SHA-pinned 295-owner Kbuild traces: no real kernel rebuild."""
    owners=[f"kernel/owner{i:03d}.o" for i in range(295)]
    anc=["drivers/aux0.o", "drivers/aux1.o"]
    def dispatch(o):
        name=o[:-2]+".minic-stage2.s"
        return "/tmp/from-preprocessor.i\t/tmp/stage4-work/"+name
    stage4_sha="50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31"
    stage2_sha="17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d"
    with TemporaryDirectory(prefix="bootstrap-guest-audit-") as tmp:
        root=Path(tmp)
        own=root/"owners"
        suc=root/"success"
        trace=root/"trace"
        own.write_text("\n".join(owners)+"\n")
        base=[sys.executable,str(Path(__file__).resolve()),"--owners",str(own),
              "--success",str(suc),"--trace",str(trace)]
        def run(stage, sha, succeeded, dispatched, must_pass, why):
            suc.write_text("\n".join(succeeded)+"\n")
            trace.write_text("\n".join(map(dispatch,dispatched))+"\n")
            proc=subprocess.run([*base,"--candidate-stage",stage,"--stage2-sha",sha],
                                capture_output=True,text=True,timeout=15,check=False)
            if (proc.returncode == 0)!=must_pass:
                raise AssertionError("audit self-test "+why+
                                     " unexpected rc="+str(proc.returncode)+
                                     " stdout="+proc.stdout+" stderr="+proc.stderr)
            if must_pass and "KBUILD_AUDIT=PASS owners=295/295" not in proc.stdout:
                raise AssertionError("missing successful audit "+why)
        run("stage2",stage2_sha,owners,owners,True,"legacy exact owner set")
        run("stage2",stage2_sha,owners+anc,owners+anc,False,"legacy rejects ancillary")
        run("stage4",stage4_sha,owners+anc+owners[:5],owners+anc,
            True,"real Kbuild ancillary + repeat retries")
        run("stage4",stage4_sha,owners[1:]+anc,owners[1:]+anc,
            False,"missing real owner")
        run("stage4",stage4_sha,owners+anc,owners,
            False,"untraced ancillary Stage4 fallback")
        run("stage4",stage4_sha,owners+anc+[f"drivers/extra{i}.o" for i in range(15)],
            owners+anc+[f"drivers/extra{i}.o" for i in range(15)],
            False,"unbounded ancillary workaround")
        run("stage4",stage2_sha,owners,owners,False,"wrong compiler SHA")
    print("BOOTSTRAP_GUEST_KBUILD_AUDIT_SELFTEST=PASS cases=7 "
          "stage2_contract_unchanged=true missing_targets_fail=true "
          "traceless_auxiliary_fails=true bounded_auxiliary_verified=true",flush=True)


def main():
    cli=argparse.ArgumentParser()
    cli.add_argument("--owners",type=Path)
    cli.add_argument("--success",type=Path)
    cli.add_argument("--trace",type=Path)
    cli.add_argument("--stage2-sha",type=str,
                     help="SHA of selected self-host compiler (legacy option name)")
    cli.add_argument("--candidate-stage",choices=("stage2","stage4"),default="stage2")
    cli.add_argument("--self-test",action="store_true")
    a=cli.parse_args()
    if a.self_test:
        self_test()
        return
    if not all((a.owners,a.success,a.trace,a.stage2_sha)):
        cli.error("--owners, --success, --trace, --stage2-sha are required")
    expected_sha={
        "stage2":"17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d",
        "stage4":"50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31",
    }
    expected=expected_sha[a.candidate_stage]
    if a.stage2_sha!=expected:
        raise RuntimeError("unexpected certified self-host "+a.candidate_stage+" compiler digest")
    owners=read_owners(a.owners)
    succeeded=[z.strip().removeprefix("./") for z in
               a.success.read_text().splitlines() if z.strip()]
    successes=Counter(succeeded)
    missing_success=set(owners)-set(successes)
    extra_success=set(successes)-set(owners)
    if missing_success:
        raise RuntimeError("missing certified "+a.candidate_stage+" owner success"+
                           f" count={len(missing_success)} sample={sorted(missing_success)[:8]}")
    # The historic Stage2 contract expected an EXACT per-owner success set.
    # In the Stage4 2064 real Kbuild run, the existing cohort independently
    # records a few required ancillary MiniC compilations in the same append-
    # only success trace (route audit: 295 C owners + 2 ancillary).  Reject any
    # missing owner and allow only a small, fully guest-traced extra set.
    if extra_success and (a.candidate_stage!="stage4" or len(extra_success)>16):
        raise RuntimeError("unexpected non-owner compiler successes count="+
                           str(len(extra_success))+" sample="+
                           repr(sorted(extra_success)[:8]))
    pairs=[]
    for line in a.trace.read_text().splitlines():
        pair=line.split("\t")
        if len(pair)!=2 or not pair[0] or not pair[1]:
            raise RuntimeError("malformed Stage2 dispatch trace")
        pairs.append(tuple(pair))
    outputs=Counter(output.removeprefix("./") for _input,output in pairs)
    missing=[]
    for owner in owners:
        assembly=owner[:-2]+".minic-stage2.s"
        # Output can be an absolute path into restored GNU reference tree,
        # or a Kbuild-relative path. A suffix after '/' is an exact owner.
        num=sum(n for output,n in outputs.items()
                if output == assembly or output.endswith("/"+assembly))
        if not num:
            missing.append(owner)
    if missing:
        raise RuntimeError("missing "+a.candidate_stage+" QEMU compiler dispatch for "+
                           str(len(missing))+" actual Kbuild C targets: "+
                           repr(missing[:10]))
    untraced=[]
    for extra in sorted(extra_success):
        if not extra.endswith(".o"):
            raise RuntimeError("non-object ancillary Stage4 success "+extra)
        asm=extra[:-2]+".minic-stage2.s"
        if not any(out==asm or out.endswith("/"+asm) for out in outputs):
            untraced.append(extra)
    if untraced:
        raise RuntimeError("ancillary successes without certified guest dispatch "+
                           repr(untraced[:8]))
    print("BOOTSTRAP_"+a.candidate_stage.upper()+"_KBUILD_AUDIT=PASS owners="+str(len(owners))+
          "/"+str(len(owners))+
          " qemu_"+a.candidate_stage+"_asm_dispatch="+str(len(owners))+
          " original_stage0_fallback=0"
          " ancillary_guest_verified="+str(len(extra_success))+
          " "+a.candidate_stage+"_sha256="+expected,flush=True)


if __name__=="__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        print("BOOTSTRAP_SELFHOST_KBUILD_AUDIT=FAIL "+str(exc),
              file=sys.stderr,flush=True)
        sys.exit(1)

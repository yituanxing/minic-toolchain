#!/usr/bin/env python3
"""Regression: a Linux C translation unit exposes ALL Core failures.

One first-error diagnostic remains the public C compiler contract; the opt-in
corpus collector must never silently truncate the independent function list.
"""
import importlib.util
from pathlib import Path

p=Path(__file__).with_name("linux-three-isa-seven-probe-v1.py")
spec=importlib.util.spec_from_file_location("minic_census_probe",p)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
log=(
    "MINIC_CORE_FUNCTION_FAILURE kind=UNSUPPORTED index=7 function=foo\n"
    "/source.i:1:1: error: Core IR does not yet support function 'foo'\n"
    "MINIC_CORE_FUNCTION_FAILURE kind=ERROR index=13 function=bar\n"
    "MINIC_CORE_FUNCTION_FAILURE kind=UNSUPPORTED index=19 function=baz\n"
)
actual=mod.all_core_function_frontiers(log)
assert actual==[
    dict(kind="UNSUPPORTED",index=7,function="foo"),
    dict(kind="ERROR",index=13,function="bar"),
    dict(kind="UNSUPPORTED",index=19,function="baz")], actual
assert mod.all_core_function_frontiers("normal single error without census") == []
assert mod.all_core_function_frontiers(
    "MINIC_CORE_FUNCTION_FAILURE kind=UNSUPPORTED index=-1 function=bad\n") == []
trace=(
    "MINIC_M8_OPCODE_REJECT arch=aarch64 function=foo block=1 instruction=2 kind=43\n"
    "MINIC_M8_OPCODE_REJECT arch=loongarch64 function=bar block=6 instruction=9 kind=12\n"
    "MINIC_M8_OPCODE_REJECT arch=aarch64 function=baz block=3 instruction=7 kind=43\n")
assert mod.all_machine_opcode_frontiers(trace,"aarch64")==[
    dict(function="foo",block=1,instruction=2,kind=43),
    dict(function="baz",block=3,instruction=7,kind=43)]
assert mod.all_machine_opcode_frontiers(trace,"loongarch64")==[
    dict(function="bar",block=6,instruction=9,kind=12)]
assert mod.all_backend_reject_frontiers(
    "MINIC_BACKEND_REJECT arch=aarch64 function=foo stage=emit "
    "block=3 instruction=7 kind=43 backend_line=156\n","aarch64")==[
        dict(function="foo",stage="emit",block=3,instruction=7,
             kind=43,backend_line=156)]
assert mod.all_preflight_roots(
    "MINIC_BACKEND_PREFLIGHT_ROOT arch=loongarch64 "
    "function=alpha reason=gnu_asm_qualification blocks=4 "
    "parameters=3 inline=2\n","loongarch64")==[
        dict(function="alpha",reason="gnu_asm_qualification",blocks=4,
             parameters=3,inline=2)]
assert mod.all_asm_form_frontiers(
    "MINIC_BACKEND_ASM_FORM arch=aarch64 function=trace id=2 "
    "template_length=9 hash=0123456789abcdef preview=ldar_x "
    "clobbers=0 memory=1 goto=0\n","aarch64")==[
        dict(function="trace",id=2,template_length=9,
             hash="0123456789abcdef",preview="ldar_x",clobbers=0,
             memory=1,goto=0)]
assert mod.all_machine_opcode_frontiers("no trace","x86_64")==[]
source_trace=(
    "CORE_FAST_TRACE stage=statement reason=lower-status "
    "function=ns_ioctl kind=5 status=1 span=92:4 break_target=0\n"
    "CORE_FAST_TRACE stage=statement reason=lower-status "
    "function=ns_ioctl kind=5 status=1 span=92:4 break_target=0\n"
    "CORE_FAST_TRACE stage=statement reason=unsupported-kind "
    "function=tty_ioctl kind=17 span=1:1\n"
    "CORE_FAST_TRACE stage=statement reason=unsupported-kind "
    "function=bad.name kind=0\n"
)
assert mod.all_core_lower_trace_frontiers(source_trace)==[
    dict(function="ns_ioctl",stage="statement",reason="lower-status",kind="5",status="1"),
    dict(function="tty_ioctl",stage="statement",reason="unsupported-kind",kind="17",status="na")
]
assert mod.all_core_lower_trace_frontiers("no trace")==[]
print("THREE_LINUX_ALL_FRONTIERS_SELFTEST=PASS three_independent_failures=1 normal_unchanged=1 machine_trace=3")

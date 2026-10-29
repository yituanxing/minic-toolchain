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
structured_reject=(
    "MINIC_AARCH64_STRUCTURED_REJECT function=__kern_hyp_va "
    "dynamic_frame=0 template_length=42 operand_count=2 "
    "volatile=1 memory=1 goto=0 preview=msr sctlr_el1, %0\n"
)
assert mod.all_aarch64_structured_rejects(structured_reject,"aarch64")==[
    dict(function="__kern_hyp_va",dynamic_frame=0,template_length=42,
         operand_count=2,volatile=1,memory=1,goto=0,
         preview="msr sctlr_el1, %0")]
assert mod.all_aarch64_structured_rejects(structured_reject,"x86_64")==[]
assert mod.all_aarch64_structured_rejects(
    structured_reject.replace("operand_count=2", "operand_count=-1"),
    "aarch64")==[]
load_shape_log=(
    "MINIC_AARCH64_LOAD_SHAPE_REJECT result_base=1 "
    "result_rank=5 result_ptr=0 result_enum=18446744073709551615 "
    "address_id=6 address_base=2 address_ptr=1\n"
    "MINIC_AARCH64_LOAD_SHAPE_REJECT result_base=17 "
    "result_rank=0 result_ptr=0 result_enum=0 "
    "address_id=4 address_base=-1 address_ptr=0\n")
assert mod.all_load_shape_frontiers(load_shape_log,"aarch64")==[
    dict(result_base=1,result_rank=5,result_ptr=0,
         result_enum=18446744073709551615,address_id=6,
         address_base=2,address_ptr=1),
    dict(result_base=17,result_rank=0,result_ptr=0,
         result_enum=0,address_id=4,address_base=-1,address_ptr=0)]
assert mod.all_load_shape_frontiers(load_shape_log,"loongarch64")==[]
assert mod.all_load_shape_frontiers(
    "MINIC_AARCH64_LOAD_SHAPE_REJECT result_base=X "
    "result_rank=0 result_ptr=0 result_enum=0 "
    "address_id=4 address_base=0 address_ptr=1\n","aarch64")==[]
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
asm_frontier=(
    "CORE_ASM_FRONTIER function=ns_ioctl reason=target_required "
    "template_length=7 template_hash=0123456789abcdef "
    "signature_hash=1111111111111111 preview=msr_x0 outputs=0 "
    "inputs=1 labels=0 reg_clobbers=0 clobbers=1 memory=1 goto=0 volatile=1\n"
    "CORE_ASM_FRONTIER function=ns_ioctl reason=target_required "
    "template_length=7 template_hash=0123456789abcdef "
    "signature_hash=1111111111111111 preview=msr_x0 outputs=0 "
    "inputs=1 labels=0 reg_clobbers=0 clobbers=1 memory=1 goto=0 volatile=1\n"
    "CORE_ASM_FRONTIER function=ns_ioctl reason=target_lower "
    "template_length=7 template_hash=0123456789abcdef "
    "signature_hash=2222222222222222 preview=msr_x0 outputs=1 "
    "inputs=1 labels=0 reg_clobbers=0 clobbers=1 memory=1 goto=0 volatile=1\n"
)
assert mod.all_core_asm_frontiers(asm_frontier)==[
    dict(function="ns_ioctl",reason="target_required",template_length=7,
         template_hash="0123456789abcdef",signature_hash="1111111111111111",
         preview="msr_x0",outputs=0,inputs=1,labels=0,reg_clobbers=0,
         clobbers=1,memory=1,goto=0,volatile=1),
    dict(function="ns_ioctl",reason="target_lower",template_length=7,
         template_hash="0123456789abcdef",signature_hash="2222222222222222",
         preview="msr_x0",outputs=1,inputs=1,labels=0,reg_clobbers=0,
         clobbers=1,memory=1,goto=0,volatile=1)]
assert mod.all_core_asm_frontiers("no trace") == []
assert mod.all_core_asm_frontiers(
    asm_frontier.replace("signature_hash=1111111111111111",
                         "signature_hash=INVALID_HASH")) == [
          mod.all_core_asm_frontiers(asm_frontier)[1]]
print("THREE_LINUX_ALL_FRONTIERS_SELFTEST=PASS three_independent_failures=1 normal_unchanged=1 machine_trace=3 load_shape_trace=2 structured_reject=1")

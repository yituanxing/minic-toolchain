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
print("THREE_LINUX_ALL_FRONTIERS_SELFTEST=PASS three_independent_failures=1 normal_unchanged=1")

#!/usr/bin/env python3
from pathlib import Path

header_path = Path("src/frontend/attribute.h")
header = header_path.read_text()
if "MINIC_ATTRIBUTE_NO_SANITIZE_ADDRESS" not in header:
    old = """    MINIC_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,
    MINIC_ATTRIBUTE_ALWAYS_INLINE,
"""
    new = """    MINIC_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,
    MINIC_ATTRIBUTE_NO_SANITIZE_ADDRESS,
    MINIC_ATTRIBUTE_NO_STACK_PROTECTOR,
    MINIC_ATTRIBUTE_ALWAYS_INLINE,
"""
    if header.count(old) != 1:
        raise SystemExit("unexpected attribute enum anchor")
    header = header.replace(old, new, 1)
    header_path.write_text(header)

source_path = Path("src/frontend/attribute.c")
source = source_path.read_text()
if '"__no_sanitize_address__"' not in source:
    old = """    MINIC_ATTRIBUTE_ENTRY("__no_instrument_function__",
                          MINIC_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,
                          MINIC_ATTRIBUTE_CLASS_INFORMATIONAL,
                          MINIC_ATTRIBUTE_TARGET_FUNCTION),
"""
    new = old + """    {
        "no_sanitize_address",
        sizeof("no_sanitize_address") - 1U,
        MINIC_ATTRIBUTE_NO_SANITIZE_ADDRESS,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
    {
        "__no_sanitize_address__",
        sizeof("__no_sanitize_address__") - 1U,
        MINIC_ATTRIBUTE_NO_SANITIZE_ADDRESS,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
    {
        "no_stack_protector",
        sizeof("no_stack_protector") - 1U,
        MINIC_ATTRIBUTE_NO_STACK_PROTECTOR,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
    {
        "__no_stack_protector__",
        sizeof("__no_stack_protector__") - 1U,
        MINIC_ATTRIBUTE_NO_STACK_PROTECTOR,
        MINIC_ATTRIBUTE_CLASS_OPTIMIZATION,
        MINIC_ATTRIBUTE_TARGET_FUNCTION,
        0U,
        0U,
        true,
    },
"""
    if source.count(old) != 1:
        raise SystemExit("unexpected attribute registry anchor")
    source = source.replace(old, new, 1)
    source_path.write_text(source)

fixture_path = Path("tests/compiler/c0/gnu_prefix_function_attributes.c")
fixture = fixture_path.read_text()
if "__no_sanitize_address__" not in fixture:
    old = """__attribute__((__externally_visible__)) __attribute__((__cold__))
__attribute__((__section__(".probe.externally-visible.text")))
void externally_visible_decl(int value)
"""
    new = """__attribute__((__externally_visible__)) __attribute__((__cold__))
__attribute__((__section__(".probe.externally-visible.text")))
__attribute__((__no_sanitize_address__)) __attribute__((__no_stack_protector__))
void externally_visible_decl(int value)
"""
    if fixture.count(old) != 1:
        raise SystemExit("unexpected prefix function attribute fixture anchor")
    fixture = fixture.replace(old, new, 1)
    fixture += """

__attribute__((no_sanitize_address)) __attribute__((no_stack_protector))
void instrumentation_policy_aliases(void)
{
}
"""
    fixture_path.write_text(fixture)

run_path = Path("tests/compiler/c0/run-gnu-prefix-function-attributes.sh")
run = run_path.read_text()
if "instrumentation_policy_aliases:" not in run:
    anchor = "grep -F 'externally_visible_decl:' \"$assembly\" >/dev/null\n"
    replacement = anchor + "grep -F 'instrumentation_policy_aliases:' \"$assembly\" >/dev/null\n"
    if run.count(anchor) != 1:
        raise SystemExit("unexpected prefix function attribute gate anchor")
    run = run.replace(anchor, replacement, 1)
    run = run.replace(
        "gnu-inline=static-only'",
        "gnu-inline=static-only no-sanitize-address=parse-only no-stack-protector=parse-only'",
    )
    run_path.write_text(run)

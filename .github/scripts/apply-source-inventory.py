from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    content = path.read_text(encoding="utf-8")
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected one replacement, found {count}: {old!r}"
        )
    path.write_text(content.replace(old, new, 1), encoding="utf-8")


makefile = Path("Makefile")
replace_once(
    makefile,
    "\tcheck-type check-record check-type-alias check-ast-contract check-layout \\\n"
    "\tcheck-static-functions \\\n",
    "\tcheck-type check-record check-type-alias check-ast-contract check-layout \\\n"
    "\tcheck-source-inventory check-static-functions \\\n",
)
replace_once(
    makefile,
    "\tcheck-runtime sanitize bootstrap bootstrap-compare format format-check \\\n"
    "\tclean distclean print-config\n",
    "\tcheck-runtime sanitize bootstrap bootstrap-compare format format-check \\\n"
    "\tclean distclean print-config print-minic-sources\n",
)
replace_once(
    makefile,
    "\t\t\"  make check-fast         Run the fast frontend and C0 gates\" \\\n"
    "\t\t\"  make check-token-model  Run the token data-model unit gate\" \\\n",
    "\t\t\"  make check-fast         Run the fast frontend and C0 gates\" \\\n"
    "\t\t\"  make check-source-inventory Verify production C source coverage\" \\\n"
    "\t\t\"  make check-token-model  Run the token data-model unit gate\" \\\n",
)
replace_once(
    makefile,
    "check-layout: $(LAYOUT_TEST_BINARY)\n"
    "\t\"$(abspath $(LAYOUT_TEST_BINARY))\"\n\n"
    "check-static-functions: $(MINIC_BINARY)\n",
    "check-layout: $(LAYOUT_TEST_BINARY)\n"
    "\t\"$(abspath $(LAYOUT_TEST_BINARY))\"\n\n"
    "check-source-inventory:\n"
    "\tsh tools/check-source-inventory.sh\n\n"
    "check-static-functions: $(MINIC_BINARY)\n",
)
replace_once(
    makefile,
    "check-fast: check-token-model check-lexer check-type check-record check-type-alias check-ast-contract check-layout check-static-functions",
    "check-fast: check-source-inventory check-token-model check-lexer check-type check-record check-type-alias check-ast-contract check-layout check-static-functions",
)
replace_once(
    makefile,
    "format-check:\n"
    "\t@printf '%s\\n' \"format-check: formatter policy is not automated yet\"\n\n"
    "print-config:\n",
    "format-check:\n"
    "\t@printf '%s\\n' \"format-check: formatter policy is not automated yet\"\n\n"
    "print-minic-sources:\n"
    "\t@printf '%s\\n' $(MINIC_SOURCES)\n\n"
    "print-config:\n",
)

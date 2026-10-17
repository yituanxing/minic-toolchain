#!/usr/bin/env python3
"""Benchmark-only exact revert of the integrated AST owner pass.

Creates historical baseline source in a disposable Actions checkout without
requiring old Git history or a potentially stale artifact. NEVER apply in
production: expected output blob must equal pre-optimization source SHA.
"""
from pathlib import Path
import hashlib

p=Path("src/frontend/ast_verifier.c")
s=p.read_text()
def once(old,new):
    global s
    if s.count(old)!=1:
        raise SystemExit("AST_REVERT=ANCHOR_MISMATCH count=%d key=%r"%
                         (s.count(old),old[:90]))
    s=s.replace(old,new,1)
begin="\n/* A type can own a descriptor by value (only expression / typedef / extern\n"
end="static bool function_alias_signature_matches(const MinicC0Program *program,\n"
assert s.count(begin)==1 and s.count(end)==1
a,b=s.split(begin,1)
assert end in b
_,tail=b.split(end,1)
s=a+end+tail
once("    MinicC0AstVerifyStage stage;\n    unsigned char *array_owners = NULL;\n",
     "    MinicC0AstVerifyStage stage;\n")
once("        free(array_owners);                                                                    \\\n", "")
once("    /* The storage contract is checked above before indexing any Program arrays. */\n"
     "    array_owners = minic_verify_build_array_owners(program);\n", "")
once("            !(array_owners != NULL ? array_owners[index] != 0U\n"
     "                                    : incomplete_array_has_semantic_owner(program, index))) {",
     "            !incomplete_array_has_semantic_owner(program, index)) {")
once("            free(array_owners);\n            return false;\n",
     "            return false;\n")
once("#undef MINIC_AST_VERIFY_FAIL\n    free(array_owners);\n    return true;",
     "#undef MINIC_AST_VERIFY_FAIL\n    return true;")
data=s.encode()
digest=hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()
if digest!="f3e023c69f5bcbd7093cffb8f68cf6ced46d5d49":
    raise SystemExit("AST_REVERT=WRONG_SOURCE_SHA expected_f3e0 got="+digest)
p.write_bytes(data)
print("MINIC_PERF_AST_OWNER_REVERT=PASS old_blob_sha="+digest)

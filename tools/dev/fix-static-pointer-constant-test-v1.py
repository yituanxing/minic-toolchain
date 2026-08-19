from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "tests/compiler/c0/static_pointer_constant_conditional.c"
text = path.read_text()
old = '''static void *same_function_conditional =\n    main == (void *)0 ? (void *)&main : (void *)&main;\n\n'''
new = '''static int static_pointer_function(void) {\n    return 0;\n}\nstatic void *same_function_conditional =\n    static_pointer_function == (void *)0 ? (void *)&static_pointer_function\n                                         : (void *)&static_pointer_function;\n\n'''
if old not in text:
    raise SystemExit("same-function focused canary anchor missing")
path.write_text(text.replace(old, new, 1))

runner_path = root / "tests/compiler/c0/run-static-pointer-constant-conditional.sh"
runner = runner_path.read_text()
old_runner = "same_function_count=$(grep -F -c '  .dword main' \"$work/output.s\")"
new_runner = "same_function_count=$(grep -F -c '  .dword static_pointer_function' \"$work/output.s\")"
if old_runner not in runner:
    raise SystemExit("same-function relocation runner anchor missing")
runner_path.write_text(runner.replace(old_runner, new_runner, 1))
print("fixed same-function static pointer focused canary")

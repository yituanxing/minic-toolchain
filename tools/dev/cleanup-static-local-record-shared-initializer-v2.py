from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_statement.c"
text = path.read_text()
start_marker = "static bool aggregate_expression_is_zero_constant(const MinicC0Program *program,\n"
end_marker = "static bool add_zero_assignment_to_lvalue(MinicParser *parser,\n"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("legacy zero-aggregate helper chain shape changed")
dead = text[start:end]
for name in (
    "aggregate_expression_is_zero_constant",
    "parse_zero_aggregate_initializer_contents",
    "parse_zero_aggregate_initializer",
):
    if dead.count(name) < 1:
        raise SystemExit(f"expected dead helper {name} in cleanup range")
# These helpers were private to the retired static-local record field walker.
# The runtime aggregate initializer below starts at add_zero_assignment_to_lvalue
# and remains live, so delete only the now-unreachable parse-only zero chain.
path.write_text(text[:start] + text[end:])

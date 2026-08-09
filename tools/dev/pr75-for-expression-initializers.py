#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = '''    } else if (!parse_expression_or_assignment_statement(parser, false)) {
        return false;
    }
'''
new = '''    } else if (!parse_expression_or_assignment_statement(parser, true)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"for expression initializer dispatch: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged general expression statements in for initializers")

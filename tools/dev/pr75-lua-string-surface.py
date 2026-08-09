#!/usr/bin/env python3
from pathlib import Path

path = Path("tests/external/lua/probe.sh")
text = path.read_text()
old = '''char *strrchr(const char *string, int character);\nchar *strstr(const char *string, const char *needle);\nchar *strerror(int error_number);\n'''
new = '''char *strrchr(const char *string, int character);\nchar *strstr(const char *string, const char *needle);\nchar *strpbrk(const char *string, const char *accept);\nsize_t strspn(const char *string, const char *accept);\nchar *strerror(int error_number);\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"Lua controlled string surface: expected 1 match, found {count}")
path.write_text(text.replace(old, new, 1))
print("staged Lua C string strpbrk/strspn declarations")

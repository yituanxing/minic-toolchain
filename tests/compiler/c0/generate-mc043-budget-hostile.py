#!/usr/bin/env python3
"""Reproducible GNU C quota boundaries and mandatory-inline hostile probes."""

import argparse


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=("integer", "boolean", "constant-p", "return-address"),
                        required=True)
    parser.add_argument("--count", type=int, required=True)
    args = parser.parse_args()
    if not 1 <= args.count <= 4096:
        parser.error("count must be in [1,4096]")

    if args.kind == "return-address":
        print("static inline __attribute__((always_inline)) void *probe(int x) {")
        print("  (void)x;")
        print("  return __builtin_return_address(0);")
        print("}")
        print("int test(int active) {")
        print("  void *volatile sink = (void *)0;")
        print("  (void)active;")
        for n in range(args.count - 1):
            print(f"  sink = probe({n});")
        print("  (void)sink;")
        print(f"  return probe({args.count - 1}) == __builtin_return_address(0);")
    else:
        if args.kind == "boolean":
            print("static inline __attribute__((always_inline)) int probe(_Bool b, int x) {")
            print("  return b ? x : 0;")
            print("}")
            call = lambda n: f"probe((_Bool)(active != 0), {n})"
        else:
            print("static inline __attribute__((always_inline)) int probe(int x) {")
            if args.kind == "constant-p":
                print("  return __builtin_constant_p(x) ? x + 1 : x;")
            else:
                print("  return x;")
            print("}")
            call = lambda n: f"probe({n})"
        print("int test(int active) {")
        print("  int sum = active - active;")
        for n in range(args.count):
            print(f"  sum += {call(n)};")
        expected = args.count * (args.count - 1) // 2
        if args.kind == "constant-p":
            expected += args.count
        print(f"  return sum == {expected};")
    print("}")
    print("int main(void) { return test(1) ? 0 : 1; }")


if __name__ == "__main__":
    main()

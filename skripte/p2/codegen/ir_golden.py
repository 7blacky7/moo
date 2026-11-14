#!/usr/bin/env python3
"""P2 native integer IR structural golden: standalone, no compiler invocation."""
from __future__ import annotations
import argparse
import re
import sys
from pathlib import Path

# Each probe contains exactly one intended scalar operation. Result shape, not
# block numbering or an exact error-helper name, is the stable golden.
PROBES = {
    "p2_add": ("add", "i8", False),
    "p2_sub": ("sub", "i8", False),
    "p2_mul": ("mul", "i8", False),
    "p2_neg": ("sub", "i8", False),
    "p2_div": ("sdiv", "i64", True),
    "p2_rem": ("srem", "i64", True),
    "p2_shl": ("shl", "i64", True),
    "p2_ashr": ("ashr", "i64", True),
    "p2_lshr": ("lshr", "i64", True),
}
FUNCTION = re.compile(r'^define\b[^\n]*@(?:"([^"]+)"|([\w.$-]+))\s*\([^\n]*\)[^\n]*\{\s*$', re.M)
OP = re.compile(r'^\s*%[-\w.$]+\s*=\s*(add|sub|mul|sdiv|srem|shl|ashr|lshr)\s+((?:(?:nsw|nuw|exact|disjoint)\s+)*)((?:i\d+))\b', re.M)

def check(ir: str) -> list[str]:
    matches = list(FUNCTION.finditer(ir))
    functions: dict[str, str] = {}
    errors: list[str] = []
    for m in matches:
        end = re.search(r'^}\s*$', ir[m.end():], re.M)
        if not end:
            errors.append("unterminated function " + (m.group(1) or m.group(2)))
            continue
        name = m.group(1) or m.group(2)
        if name in functions:
            errors.append("duplicate function " + name)
        functions[name] = ir[m.end():m.end() + end.start()]
    for name, (opcode, width, guarded) in PROBES.items():
        body = functions.get(name)
        if body is None:
            errors.append(name + ": missing native definition")
            continue
        ops = list(OP.finditer(body))
        found = [m for m in ops if m.group(1) == opcode and m.group(3) == width]
        if len(found) != 1:
            errors.append(f"{name}: expected exactly one {opcode} {width}, found {len(found)}")
        for m in ops:
            if m.group(1) in ("add", "sub", "mul") and re.search(r'\b(?:nsw|nuw)\b', m.group(2)):
                errors.append(name + ": overflow promise on wrapping operation")
            if m.group(1) in ("sdiv", "srem", "shl", "ashr", "lshr") and re.search(r'\b(?:exact|nuw|nsw)\b', m.group(2)):
                errors.append(name + ": unjustified poison flag")
        if guarded and (not re.search(r'\bicmp\b', body) or not re.search(r'\bbr\s+i1\b', body)):
            errors.append(name + ": missing comparison/conditional branch")
        # These comparisons are structural only, not a dominance proof.
        comparisons = "\n".join(line for line in body.splitlines() if re.search(r'\bicmp\b', line))
        required = {
            "p2_div": ("0", "-1", "-9223372036854775808"),
            "p2_rem": ("0", "-1", "-9223372036854775808"),
        }.get(name, ())
        for value in required:
            if not re.search(r'(?<![\w-])' + re.escape(value) + r'(?![\w])', comparisons):
                errors.append(f"{name}: missing guard boundary {value}")
        if name in ("p2_shl", "p2_ashr", "p2_lshr") and not re.search(r'(?<![\w-])(?:63|64)(?![\w])', comparisons):
            errors.append(name + ": missing shift-width boundary 63/64")
        if re.search(r'@moo_(?:add|sub|mul|div|mod|lshift|rshift)\b', body):
            errors.append(name + ": dynamic arithmetic in native probe")
    return errors

def fixture() -> str:
    result = []
    for name, (op, typ, guard) in PROBES.items():
        checks = ""
        if name in ("p2_div", "p2_rem"):
            checks = "  %z = icmp eq i64 %b, 0\n  %m = icmp eq i64 %a, -9223372036854775808\n  %n = icmp eq i64 %b, -1\n"
        elif guard:
            checks = "  %z = icmp ult i64 %b, 64\n"
        prefix = checks + "  br i1 %z, label %fail, label %ok\nok:\n" if guard else ""
        result.append(f"define {typ} @{name}({typ} %a, {typ} %b) {{\nentry:\n{prefix}  %r = {op} {typ} %a, %b\n  ret {typ} %r\n}}\n")
    return "\n".join(result)

def self_test() -> int:
    text = fixture()
    assert not check(text), check(text)
    assert any("overflow promise" in e for e in check(text.replace("add i8", "add nsw i8", 1)))
    assert any("missing native definition" in e for e in check(text.replace("@p2_div(", "@p2_missing(", 1)))
    assert any("conditional branch" in e for e in check(text.replace("br i1 %z, label %fail, label %ok", "br label %ok")))
    assert any("missing guard boundary -1" in e for e in check(text.replace("%n = icmp eq i64 %b, -1", "%n = icmp eq i64 %b, 1")))
    assert any("missing shift-width boundary" in e for e in check(text.replace("icmp ult i64 %b, 64", "icmp ult i64 %b, 65")))
    print("checker self-test: 6 cases OK (synthetic IR; not compiler validation)")
    return 0

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ir", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test == (args.ir is not None):
        parser.error("choose exactly one of --ir or --self-test")
    if args.self_test:
        return self_test()
    try:
        errors = check(args.ir.read_text(encoding="utf-8"))
    except OSError as exc:
        print(exc, file=sys.stderr)
        return 2
    if errors:
        for err in errors:
            print("FAIL:", err, file=sys.stderr)
        return 1
    print("IR structural golden passed; LLVM verify and runtime error-path tests still required")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

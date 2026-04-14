#!/usr/bin/env python3
"""
check_tokens.py — verifiziert dass alle in spec/tokens.yaml deklarierten Token-Varianten
in den jeweils als `implementations:` markierten Quellen tatsächlich vorhanden sind.

Rückgabecode:
  0 — alles synchron
  1 — mindestens eine Variante fehlt (Breaking-Reduktion)

Dieses Skript ist die erste Stufe der Token-Single-Source-of-Truth (PRIO 1 aus dem
Konsolidierungs-Plan). Es generiert noch nichts — es *verhindert nur Regressionen*.
Volle Codegen (gen_tokens.rs/py) kann darauf aufgebaut werden, ohne Breaking-Change-Risiko.

Aufruf:
  python3 tools/check_tokens.py           # alles prüfen
  python3 tools/check_tokens.py --list    # nur zusammenfassen
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import textwrap

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = ROOT / "spec" / "tokens.yaml"
RUST_TOKENS = ROOT / "compiler" / "src" / "tokens.rs"
PY_TOKENS = ROOT / "src" / "moo" / "tokens.py"


def parse_yaml_minimal(path: pathlib.Path) -> list[dict]:
    """
    Minimal-Parser für das tokens.yaml-Schema (ohne externe Abhängigkeit).
    Akzeptiert nur die Konstrukte, die wir selbst schreiben.
    """
    tokens: list[dict] = []
    current: dict | None = None
    in_variants = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        # neuer Token-Eintrag
        m = re.match(r"^\s{2}- id:\s*(\S+)", line)
        if m:
            if current is not None:
                tokens.append(current)
            current = {"id": m.group(1), "variants": []}
            in_variants = False
            continue
        if current is None:
            continue
        # skalare Felder
        m = re.match(r"^\s{4}(\w+):\s*(.*)$", line)
        if m:
            key, val = m.group(1), m.group(2).strip()
            if key == "variants":
                in_variants = True
                if val.startswith("[") and val.endswith("]"):
                    items = [s.strip().strip('"').strip("'") for s in val[1:-1].split(",")]
                    current["variants"] = [s for s in items if s]
                    in_variants = False
                else:
                    current["variants"] = []
                continue
            if key == "implementations":
                if val.startswith("[") and val.endswith("]"):
                    current["implementations"] = [s.strip() for s in val[1:-1].split(",")]
                else:
                    current["implementations"] = []
                continue
            current[key] = val
            continue
    if current is not None:
        tokens.append(current)
    return tokens


def load_rust_variants() -> set[str]:
    """Extrahiert alle String-Literale aus compiler/src/tokens.rs keyword_lookup()."""
    if not RUST_TOKENS.exists():
        return set()
    text = RUST_TOKENS.read_text(encoding="utf-8")
    variants = set()
    # Match any pattern like "...tok..." optionally with | chains, followed by => Some(TokenType::
    # We scan per-line: any "x" before => Some(TokenType::...) counts as a variant.
    for raw in text.splitlines():
        if "=> Some(TokenType::" not in raw:
            continue
        pat = raw.split("=>", 1)[0]
        for m in re.finditer(r'"([^"\n]+)"', pat):
            variants.add(m.group(1))
    return variants


def load_python_variants() -> set[str]:
    """Extrahiert alle Keys aus KEYWORDS dict in src/moo/tokens.py."""
    if not PY_TOKENS.exists():
        return set()
    text = PY_TOKENS.read_text(encoding="utf-8")
    variants = set()
    m = re.search(r"KEYWORDS[^=]*=\s*\{(.*?)^\}", text, re.S | re.M)
    if not m:
        return variants
    for km in re.finditer(r'"([^"\n]+)"\s*:\s*TokenType\.', m.group(1)):
        variants.add(km.group(1))
    return variants


def main() -> int:
    ap = argparse.ArgumentParser(description="Token-Spec-Verifier für moo")
    ap.add_argument("--list", action="store_true", help="Zusammenfassung drucken, nicht strikt prüfen")
    args = ap.parse_args()

    tokens = parse_yaml_minimal(SPEC)
    rust = load_rust_variants()
    py = load_python_variants()

    print(f"[spec]   {len(tokens)} Token-Einträge in {SPEC.relative_to(ROOT)}")
    print(f"[rust]   {len(rust)} String-Varianten in {RUST_TOKENS.relative_to(ROOT)}")
    print(f"[python] {len(py)} String-Varianten in {PY_TOKENS.relative_to(ROOT)}")

    errors: list[str] = []
    for t in tokens:
        impl = set(t.get("implementations", []))
        for variant in t.get("variants", []):
            if not variant or variant[0] in '+-*/%=<>.!&|^~@?()[]{}:,':
                continue  # Operatoren/Delimiter überspringen (andere Kanäle)
            if "rust" in impl and variant not in rust:
                errors.append(f"{t['id']}: Variante '{variant}' laut YAML in Rust erwartet, fehlt in tokens.rs")
            if "python" in impl and variant not in py:
                errors.append(f"{t['id']}: Variante '{variant}' laut YAML in Python erwartet, fehlt in tokens.py")

    if args.list or not errors:
        print()
        print("Summary:")
        print(f"  active tokens:  {sum(1 for t in tokens if t.get('status') == 'active')}")
        print(f"  legacy tokens:  {sum(1 for t in tokens if t.get('status') == 'legacy')}")
        print(f"  soft keywords:  {sum(1 for t in tokens if t.get('kind') == 'soft-keyword')}")
        if args.list:
            print()
            print("Soft-Keywords (kontext-sensitiv, auch als Identifier erlaubt):")
            for t in tokens:
                if t.get("kind") == "soft-keyword":
                    vs = ", ".join(t.get("variants") or [])
                    print(f"  - {t['id']:14s}  [{vs}]")

    if errors:
        print()
        print("FEHLER (Token-Reduktion gegenüber spec/tokens.yaml):")
        for e in errors:
            print(f"  - {e}")
        print()
        print(textwrap.dedent("""\
            → Entweder die Variante in der Implementation wiederherstellen,
              oder den Eintrag in spec/tokens.yaml entfernen bzw. status: legacy setzen
              + implementations: [] setzen, wenn die Variante bewusst deaktiviert wurde."""))
        return 1

    print("\nOK — alle YAML-Varianten in ihren Zielimplementierungen vorhanden.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

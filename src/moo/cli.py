"""CLI für moo — die universelle Programmiersprache."""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from .generators.javascript import JavaScriptGenerator
from .generators.python import PythonGenerator
from .lexer import Lexer, LexerError
from .parser import ParseError, Parser


GENERATORS = {
    "python": PythonGenerator,
    "javascript": JavaScriptGenerator,
    "js": JavaScriptGenerator,
}


def compile_source(source: str, target: str = "python") -> str:
    gen_class = GENERATORS.get(target)
    if not gen_class:
        print(f"Fehler: Unbekannte Zielsprache '{target}'", file=sys.stderr)
        print(f"Verfügbar: {', '.join(GENERATORS)}", file=sys.stderr)
        sys.exit(1)

    tokens = Lexer(source).tokenize()
    ast = Parser(tokens).parse()
    return gen_class().generate(ast)


def cmd_build(args: argparse.Namespace):
    source = Path(args.file).read_text()
    output = compile_source(source, args.target)

    if args.output:
        Path(args.output).write_text(output)
        print(f"✓ {args.file} → {args.output} ({args.target})")
    else:
        print(output, end="")


def cmd_run(args: argparse.Namespace):
    source = Path(args.file).read_text()
    py_code = compile_source(source, "python")

    with tempfile.NamedTemporaryFile(mode="w", suffix=".py", delete=False) as f:
        f.write(py_code)
        f.flush()
        result = subprocess.run([sys.executable, f.name], capture_output=False)
        sys.exit(result.returncode)


def cmd_fmt(args: argparse.Namespace):
    from .formatter import format_source

    source = Path(args.file).read_text()
    try:
        formatted = format_source(source)
    except (LexerError, ParseError) as e:
        print(f"Fehler: {e}", file=sys.stderr)
        sys.exit(1)

    if args.check:
        if source == formatted:
            print(f"✓ {args.file} ist formatiert")
        else:
            print(f"✗ {args.file} ist nicht formatiert", file=sys.stderr)
            sys.exit(1)
    else:
        Path(args.file).write_text(formatted)
        print(f"✓ {args.file} formatiert")


def cmd_repl():
    print("moo REPL v0.1.0 — Tippe 'quit' zum Beenden")
    print("Zweisprachig: Deutsch & Englisch\n")

    env: dict = {}
    buffer = []
    indent_level = 0

    while True:
        try:
            prompt = "... " if indent_level > 0 else "moo> "
            line = input(prompt)
        except (EOFError, KeyboardInterrupt):
            print("\nTschüss!")
            break

        if line.strip() in ("quit", "exit", "ende"):
            print("Tschüss!")
            break

        buffer.append(line)

        # Einrückung tracken
        stripped = line.strip()
        if stripped.endswith(":"):
            indent_level += 1
            continue
        elif indent_level > 0 and stripped == "":
            indent_level = 0
        elif indent_level > 0 and not line.startswith(" ") and not line.startswith("\t"):
            indent_level = 0
        else:
            if indent_level > 0:
                continue

        source = "\n".join(buffer)
        buffer.clear()
        indent_level = 0

        if not source.strip():
            continue

        try:
            py_code = compile_source(source, "python")
            exec(py_code, env)
        except (LexerError, ParseError) as e:
            print(f"Fehler: {e}")
        except Exception as e:
            print(f"Laufzeitfehler: {e}")


def cmd_doc(args: argparse.Namespace):
    from .docgen import extract_docs, generate_markdown

    path = Path(args.file)
    all_entries: list[dict] = []

    if path.is_dir():
        for moo_file in sorted(path.rglob("*.moo")):
            source = moo_file.read_text()
            entries = extract_docs(source, str(moo_file))
            all_entries.extend(entries)
    else:
        source = path.read_text()
        all_entries = extract_docs(source, str(path))

    if not all_entries:
        print("Keine dokumentierten Funktionen/Klassen gefunden.")
        print("Tipp: Benutze ## Kommentare vor Funktionen/Klassen.")
        return

    markdown = generate_markdown(all_entries)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(markdown)
    print(f"✓ Dokumentation generiert: {output} ({len(all_entries)} Einträge)")


def main():
    ap = argparse.ArgumentParser(prog="moo", description="moo — die universelle Programmiersprache")
    sub = ap.add_subparsers(dest="command")

    # moo run <file>
    run_p = sub.add_parser("run", help="Eine .moo-Datei ausführen")
    run_p.add_argument("file", help="Die .moo-Datei")

    # moo build <file>
    build_p = sub.add_parser("build", help="Eine .moo-Datei transpilieren")
    build_p.add_argument("file", help="Die .moo-Datei")
    build_p.add_argument("-t", "--target", default="python", help="Zielsprache (python, javascript)")
    build_p.add_argument("-o", "--output", help="Ausgabedatei")

    # moo repl
    sub.add_parser("repl", help="Interaktiver Modus (REPL)")

    # moo fmt <file>
    fmt_p = sub.add_parser("fmt", help="Code formatieren")
    fmt_p.add_argument("file", help="Die .moo-Datei")
    fmt_p.add_argument("--check", action="store_true", help="Nur prüfen ob formatiert (Exit 1 wenn nicht)")

    # moo lsp
    sub.add_parser("lsp", help="Language Server starten (LSP via stdio)")

    # moo doc <file>
    doc_p = sub.add_parser("doc", help="API-Dokumentation generieren")
    doc_p.add_argument("file", help="Die .moo-Datei (oder Verzeichnis)")
    doc_p.add_argument("-o", "--output", default="docs/api.md", help="Ausgabedatei (Standard: docs/api.md)")

    args = ap.parse_args()
    if args.command == "run":
        cmd_run(args)
    elif args.command == "build":
        cmd_build(args)
    elif args.command == "fmt":
        cmd_fmt(args)
    elif args.command == "repl":
        cmd_repl()
    elif args.command == "doc":
        cmd_doc(args)
    elif args.command == "lsp":
        from moo.lsp import main as lsp_main
        lsp_main()
    elif args.command is None:
        cmd_repl()
    else:
        ap.print_help()


if __name__ == "__main__":
    main()

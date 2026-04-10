"""moo Language Server — Diagnostics, Completion, Hover via LSP.

Ehrlich: Nur Syntax-Level. Kein Typsystem, kein Go-to-Definition, kein Rename.
Nutzt den Python Lexer/Parser fuer Fehler-Erkennung.
"""

from pygls.lsp.server import LanguageServer
from lsprotocol import types
import re

from moo.lexer import Lexer, LexerError
from moo.parser import Parser, ParseError
from moo.tokens import KEYWORDS

server = LanguageServer("moo-lsp", "v0.1.0")

# === Keyword + Builtin Daten ===

MOO_KEYWORDS_DE = [
    "setze", "auf", "wenn", "sonst", "solange", "für", "in", "funktion",
    "gib_zurück", "zeige", "und", "oder", "nicht", "wahr", "falsch", "nichts",
    "klasse", "neu", "selbst", "importiere", "aus", "exportiere", "versuche",
    "fange", "wirf", "stopp", "weiter", "konstante", "prüfe", "fall",
    "standard", "aufräumen", "garantiere", "daten",
]

MOO_KEYWORDS_EN = [
    "set", "to", "if", "else", "while", "for", "in", "func", "return",
    "show", "and", "or", "not", "true", "false", "none", "class", "new",
    "this", "import", "from", "export", "try", "catch", "throw", "break",
    "continue", "const", "match", "case", "default", "defer", "guard", "data",
]

BUILTINS = {
    # Mathe
    "abs": "abs(x) — Absolutwert",
    "wurzel": "wurzel(x) / sqrt(x) — Quadratwurzel",
    "sqrt": "sqrt(x) — Square root",
    "runde": "runde(x) / round(x) — Runden",
    "round": "round(x) — Round to nearest integer",
    "boden": "boden(x) / floor(x) — Abrunden",
    "floor": "floor(x) — Round down",
    "decke": "decke(x) / ceil(x) — Aufrunden",
    "ceil": "ceil(x) — Round up",
    "min": "min(a, b) — Minimum",
    "max": "max(a, b) — Maximum",
    "zufall": "zufall() / random() — Zufallszahl 0..1",
    "random": "random() — Random number 0..1",
    # Typen
    "typ_von": "typ_von(x) / type_of(x) — Typ als Text",
    "type_of": "type_of(x) — Type as string",
    "länge": "länge(x) / len(x) — Laenge von String/Liste/Dict",
    "len": "len(x) — Length of string/list/dict",
    "text": "text(x) / str(x) — In Text umwandeln",
    "str": "str(x) — Convert to string",
    "zahl": "zahl(x) / num(x) — In Zahl umwandeln",
    "num": "num(x) — Convert to number",
    # I/O
    "zeige": "zeige(x) / show(x) — Ausgabe auf Konsole",
    "eingabe": "eingabe(prompt) / input(prompt) — Benutzereingabe",
    "input": "input(prompt) — User input",
    # Result-Typ
    "ok": "ok(wert) — Erzeugt Ok-Result (Rust-inspiriert)",
    "fehler": "fehler(msg) / err(msg) — Erzeugt Error-Result",
    "err": "err(msg) — Create error result",
    "ist_ok": "ist_ok(r) / is_ok(r) — Prueft ob Result ok ist",
    "is_ok": "is_ok(r) — Check if result is ok",
    "ist_fehler": "ist_fehler(r) / is_err(r) — Prueft ob Result ein Fehler ist",
    "is_err": "is_err(r) — Check if result is error",
    "entpacke": "entpacke(r) / unwrap(r) — Wert aus Result entpacken",
    "unwrap": "unwrap(r) — Unwrap result value",
    # Datei
    "datei_lesen": "datei_lesen(pfad) / file_read(path) — Datei lesen",
    "file_read": "file_read(path) — Read file contents",
    "datei_schreiben": "datei_schreiben(pfad, inhalt) / file_write(path, content)",
    "file_write": "file_write(path, content) — Write file",
    "datei_existiert": "datei_existiert(pfad) / file_exists(path)",
    "file_exists": "file_exists(path) — Check if file exists",
    # JSON
    "json_parse": "json_parse(text) / json_lesen(text) — JSON parsen",
    "json_string": "json_string(wert) / json_text(wert) — In JSON umwandeln",
    # HTTP
    "http_get": "http_get(url) / http_hole(url) — HTTP GET Request",
    "http_post": "http_post(url, body) / http_sende(url, body) — HTTP POST",
    # Grafik
    "fenster_erstelle": "fenster_erstelle(titel, breite, hoehe) — SDL2 Fenster",
    "window_create": "window_create(title, width, height) — SDL2 window",
    "zeichne_rechteck": "zeichne_rechteck(win, x, y, w, h, farbe) — Rechteck zeichnen",
    "draw_rect": "draw_rect(win, x, y, w, h, color) — Draw rectangle",
    # 3D
    "raum_erstelle": "raum_erstelle(titel, b, h) / space_create — 3D Fenster (OpenGL)",
    "raum_würfel": "raum_würfel(win, x, y, z, size, farbe) — 3D Wuerfel",
    "raum_kugel": "raum_kugel(win, x, y, z, radius, farbe, detail) — 3D Kugel",
    "raum_kamera": "raum_kamera(win, ex, ey, ez, lx, ly, lz) — Kamera setzen",
}


# === Diagnostics ===

def validate_document(ls: LanguageServer, uri: str, source: str):
    """Parst den Quelltext und sendet Fehler als Diagnostics."""
    diagnostics: list[types.Diagnostic] = []

    try:
        lexer = Lexer(source)
        tokens = lexer.tokenize()
    except LexerError as e:
        line = max(0, e.line - 1)
        diagnostics.append(types.Diagnostic(
            range=types.Range(
                start=types.Position(line=line, character=e.column - 1 if hasattr(e, 'column') else 0),
                end=types.Position(line=line, character=999),
            ),
            message=str(e),
            severity=types.DiagnosticSeverity.Error,
            source="moo",
        ))
        ls.text_document_publish_diagnostics(types.PublishDiagnosticsParams(
            uri=uri, diagnostics=diagnostics,
        ))
        return

    try:
        parser = Parser(tokens)
        parser.parse()
    except (ParseError, Exception) as e:
        line = 0
        msg = str(e)
        # Zeilennummer aus Fehlermeldung extrahieren
        m = re.search(r'[Zz]eile\s+(\d+)', msg)
        if m:
            line = max(0, int(m.group(1)) - 1)
        diagnostics.append(types.Diagnostic(
            range=types.Range(
                start=types.Position(line=line, character=0),
                end=types.Position(line=line, character=999),
            ),
            message=msg,
            severity=types.DiagnosticSeverity.Error,
            source="moo",
        ))

    ls.text_document_publish_diagnostics(types.PublishDiagnosticsParams(
        uri=uri, diagnostics=diagnostics,
    ))


# === LSP Event Handlers ===

@server.feature(types.TEXT_DOCUMENT_DID_OPEN)
def did_open(ls: LanguageServer, params: types.DidOpenTextDocumentParams):
    validate_document(ls, params.text_document.uri, params.text_document.text)


@server.feature(types.TEXT_DOCUMENT_DID_CHANGE)
def did_change(ls: LanguageServer, params: types.DidChangeTextDocumentParams):
    doc = ls.workspace.get_text_document(params.text_document.uri)
    validate_document(ls, params.text_document.uri, doc.source)


@server.feature(types.TEXT_DOCUMENT_DID_SAVE)
def did_save(ls: LanguageServer, params: types.DidSaveTextDocumentParams):
    doc = ls.workspace.get_text_document(params.text_document.uri)
    validate_document(ls, params.text_document.uri, doc.source)


@server.feature(types.TEXT_DOCUMENT_COMPLETION)
def completion(params: types.CompletionParams) -> types.CompletionList:
    items = []

    # Keywords (DE + EN)
    for kw in MOO_KEYWORDS_DE + MOO_KEYWORDS_EN:
        items.append(types.CompletionItem(
            label=kw,
            kind=types.CompletionItemKind.Keyword,
        ))

    # Builtins
    for name, doc in BUILTINS.items():
        items.append(types.CompletionItem(
            label=name,
            kind=types.CompletionItemKind.Function,
            detail=doc,
        ))

    return types.CompletionList(is_incomplete=False, items=items)


@server.feature(types.TEXT_DOCUMENT_HOVER)
def hover(params: types.HoverParams) -> types.Hover | None:
    doc = server.workspace.get_text_document(params.text_document.uri)
    line = doc.source.splitlines()[params.position.line] if params.position.line < len(doc.source.splitlines()) else ""

    # Wort unter dem Cursor finden
    col = params.position.character
    start = col
    while start > 0 and (line[start - 1].isalnum() or line[start - 1] in '_äöüÄÖÜß'):
        start -= 1
    end = col
    while end < len(line) and (line[end].isalnum() or line[end] in '_äöüÄÖÜß'):
        end += 1
    word = line[start:end]

    if not word:
        return None

    # Builtin-Hover
    if word in BUILTINS:
        return types.Hover(contents=types.MarkupContent(
            kind=types.MarkupKind.Markdown,
            value=f"**{word}**\n\n{BUILTINS[word]}",
        ))

    # Keyword-Hover
    if word in KEYWORDS:
        return types.Hover(contents=types.MarkupContent(
            kind=types.MarkupKind.Markdown,
            value=f"**{word}** — moo Keyword",
        ))

    return None


def main():
    """Startet den moo LSP Server auf stdio."""
    server.start_io()


if __name__ == "__main__":
    main()

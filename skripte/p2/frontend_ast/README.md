# Parser-AST-Diagnose und Tests

`moo-compiler ast DATEI` gibt den geparsten AST als menschenlesbares
Rust-Debugformat aus. Mit `--imports` werden Module wie beim Kompilieren
aufgelöst. Das Ausgabeformat ist nicht stabil und ist keine Austausch-API.
Der Befehl führt weder Codegenerierung noch das Programm aus.

```sh
mise run -C skripte/p2/frontend_ast test
mise run -C skripte/p2/frontend_ast beispiele
mise run -C skripte/p2/frontend_ast interface-import
mise run -C skripte/p2/frontend_ast rohdaten-import
mise run -C skripte/p2/frontend_ast interface-diagnose
```

`test` vergleicht geprüfte Goldendateien über einen C++-Vergleicher, prüft
dessen Negativkontrolle und verlangt konkrete Diagnosen für kaputte Typen.
Seit P2-T60 gilt das auch für Interface-Methoden: Enthält ihre Parameterliste
ein `:` (explizite Typannotation), wird ein Parserfehler mit dem Präfix
`Interface-Methode '<name>': ` gemeldet (`ast_interface_kaputt`,
`ast_interface_typ_leer`). Untypisierte Altformen ohne `:` behalten den
toleranten Fallback (`ast_interface_legacy`, `compiler/tests/test_interface.moos`);
`interface-alt` nutzt `ast_interface_legacy` als Positivkontrolle.
Abgedeckt sind einfache und verschachtelte Annotationen einschließlich
Zeigern auf Funktionstypen, native Zahlenlexeme sowie ASTs nach relativen
Funktions- und Strukturimporten. Die Goldendateien beschreiben AST-Struktur
und Quellpositionen; sie beweisen weder ABI noch ausführbare Zeiger.

`beispiele` parst rekursiv alle `.moos`-Dateien unter `beispiele/` und
prüft den Compilerhash vor und nach dem Lauf. Es löst keine Imports auf.
Die Diagnosen liegen unter `build/p2-frontend-ast/`.

`interface-import` prüft die Erhaltung einer typisierten Schnittstelle bei
vollständigem und benanntem Import. `rohdaten-import` prüft typisierte
Rohdaten bei beiden Importformen im AST und ihren Arrayinhalt im LLVM-IR.
`interface-diagnose` zeigt dagegen nur
den bestehenden Legacy-Fallback bei einem kaputten Schnittstellentyp;
sein Exitcode allein ist kein Semantik-Gate. Qualifizierte Typnamen und
Namespace-Aliase bleiben eigenständig zu prüfen.

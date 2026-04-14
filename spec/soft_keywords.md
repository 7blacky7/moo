# Soft-Keyword-Register

**Ein Soft-Keyword ist ein Name, der in einem bestimmten Kontext als Sprach-Schlüsselwort wirkt, in allen anderen Kontexten aber als gewöhnlicher Identifier (Variablen-/Funktionsname) benutzt werden darf.**

Das ist bewusst so, damit Sprachen wie moolang viele natürlich-deutsche Wörter (z.B. `zeige`, `versuche`, `daten`, `teste`) als Schlüsselwörter nutzen können, ohne den Bezeichner-Raum massiv einzuschränken.

Die Liste hier ist die verbindliche Referenz. Quelle der Wahrheit: [`spec/tokens.yaml`](tokens.yaml) (jedes Token mit `kind: soft-keyword` oder `soft: true`).

## Aktuelle Soft-Keywords

| Token | Varianten | Keyword-Kontext | Identifier erlaubt in |
|---|---|---|---|
| `Show` | `zeige`, `show`, `zeige_auf_bildschirm`, `ze` | Statement-Start (`zeige <expr>`) | Funktions-/Variablenname |
| `Try` | `versuche`, `try`, `versuche_ausfuehrung`, `ve` | Statement-Start gefolgt von `:` | Variablenname (`setze versuche auf 5`) |
| `Match` | `prüfe`, `match`, `pr` | Statement-Start mit Match-Arms | Identifier |
| `Data` | `daten`, `data` | Typ-Deklaration `daten Foo:` | Identifier |
| `Where` | `wo`, `where` | Query-Klausel `wähle … wo …` | Identifier |
| `Order` | `sortiere`, `order` | Query-Klausel `… sortiere nach …` | Identifier |
| `Test` | `teste`, `test` | Test-Block `teste "name":` | Identifier |
| `Expect` | `erwarte`, `expect` | Innerhalb `teste`-Block | Funktions-/Variablenname (siehe Bug-Notiz) |

## Gemeldete Keyword-Konflikte

Diese Fälle sind durch den Soft-Keyword-Mechanismus **noch nicht** vollständig gelöst und führen aktuell zu Fehlern mit unklaren Meldungen:

- **`erwarte`** als Funktionsname: `funktion erwarte(a, b)` → Lexer tokenisiert als `Expect`, Parser erwartet Test-Kontext. Gemeldet von k3 (2026-04-11, Synapse-Thought ddfac789). Lösung: `Expect` auch als Soft-Keyword mit "nur innerhalb `teste`-Block aktiv" behandeln.

## Regeln für neue Soft-Keywords

1. **Eintrag in `spec/tokens.yaml`** mit `kind: soft-keyword` und `soft: true`.
2. **`note:`-Feld** beschreibt den Aktivierungs-Kontext in einem Satz.
3. **Parser-Implementation** muss: Bei Lexer-Zeit trotzdem das Keyword-Token ausgeben, aber beim Parsen kontextabhängig auf Identifier zurückfallen (siehe `parser.rs:158` für das `versuche`-Muster).
4. **Fehlerdiagnose**: Wenn das Wort in einem unerwarteten Kontext als Identifier gemeint war, soll die Meldung lauten:
   > `'X' ist ein Soft-Keyword; in diesem Kontext als Identifier gemeint? Anführungsstriche oder Umbenennung (z.B. 'X_')`
5. **CI-Check**: `tools/check_tokens.py` prüft dass der YAML-Eintrag in beiden Implementierungen (Rust + Python) existiert.

## Harte Keywords (KEINE Soft-Keywords)

Zur Kontrast-Referenz — diese Wörter sind unter keinen Umständen als Identifier erlaubt:

`setze`, `auf`, `wenn`, `sonst`, `solange`, `für`, `in`, `funktion`, `gib_zurück`, `und`, `oder`, `nicht`, `wahr`, `falsch`, `nichts`, `klasse`, `neu`, `selbst`, `fange`, `wirf`, `stopp`, `weiter`, `konstante`, `fall`, `standard`, `importiere`, `aus`, `exportiere`, `als`, `aufräumen`, `garantiere`, `wähle`, `von`, `schnittstelle`, `implementiert`, `parallel`, `vorbedingung`, `nachbedingung`, `unsicher`.

(Stand: `spec/tokens.yaml` v1, 2026-04-14.)

# Moo Input-, IME- und Accessibility-Vertrag

P016-O4 definiert die Eingabe- und Semantikgrenze zwischen Host, Moo-Compositor
und `ui_moo`. Der Referenzkern ist eine deterministische C11-Zustandsmaschine
ohne Toolkit, Allokation, Syscalls, Uhr oder Hardwarezugriff.

## Schichten

- `moo_input_protocol.h`: pointerfreie, fest breite Protokollwerte für
  Pointer, Touch, Stift, physische/logische Tasten, Shortcuts, Textkomposition,
  Präferenzen und Accessibility-Aktionen. Die C-Strukturen sind ausdrücklich
  kein Wire-Format; IPC-Codecs kodieren Felder und Endianness einzeln.
- `moo_input_core.[ch]`: Fokus, Capture, physischer Key-State, Modifier,
  Touch-IDs, IME-Sessionen, Quoten, Cleanup-Reservationen und Eventreihenfolge.
- `moo_a11y_core.c`: generationierter Semantikbaum, Rollen, Zustände,
  Aktionen, Bounds, Relationen, atomare Mehrknoten-Updates, Privacy und
  Automation-Routing.
- `moo_input_compositor_bridge.[ch]`: prüft über O3 Surface-Liveness und
  Surface-Owner, bevor ein Input-Target entsteht.
- Hostadapter übersetzen ausschließlich native Events. Fokus-, IME-,
  Shortcut- und A11y-Policy bleiben im gemeinsamen Kern.
- `moo_input_win32`: normalisiert Set-1-Scancodes auf USB-HID, Pointer,
  Key-Repeats, Fokus, UTF-16/Surrogates, `WM_CHAR` und IMM-Composition. Er
  meldet nur Pointer, Keyboard, IME und Shortcuts; Touch, Stift und UIA sind
  bis zu eigenen Implementierungen ausgeschaltet.

## Öffentliche `ui_moo`-Metadaten

`uim_a11y(kontext, widget)` liefert für ein gültiges retained-mode Widget
eine neue, frei mutierbare Kopie mit exakt zehn Schlüsseln:
`version`, `uid`, `role`, `states`, `actions`, `bounds`,
`bounds_space`, `name`, `value` und `description`. Der Aufruf verändert
weder Widget noch Kontext. `nichts`, ein ungültiger Kontext oder ein
unvollständiges Widget liefern fail closed `nichts`.

Die numerischen Werte spiegeln `moo_input_protocol.h` Version 1:

- Rollen: none=0, group=2, text=3, button=4, checkbox=5, slider=6, list=8.
- States: disabled=1, focused=2, checked=4, hidden=64.
- Actions: focus=1, activate=2, increment=4, decrement=8, set-value=16,
  set-selection=32.

`focused` wird aus dem vorhandenen Fokus-Slot des übergebenen Kontextes
abgeleitet. `name` nutzt ein nichtleeres `a11y_name`, andernfalls den
Widgettext; `description` nutzt `a11y_beschreibung`. `bounds` enthält
`x`, `y`, `b` und `h`; `bounds_space` ist ausdrücklich
`parent-local`. Das ist keine Behauptung absoluter Bildschirmkoordinaten.

Diese reine Moo-Abfrage ist ein Baustein für einen späteren Hostadapter. Sie
beweist weder eine native UIA-, AT-SPI- oder NSAccessibility-Brücke noch
Plattformparität. Solche Fähigkeiten bleiben ausgeschaltet, bis ein echter
Plattformlauf sie belegt.

## Öffentliche `ui_moo`-IME-Ereigniswerte

`uim_ime_ereignis(art, text, selection_start, selection_end, revision)`
liefert einen neuen, frei mutierbaren Wert mit exakt sechs Schlüsseln:
`version`, `art`, `text`, `selection_start`, `selection_end` und `revision`.
Version 1 kennt ausschließlich `preedit`, `commit` und `cancel`.

Auswahlgrenzen sind UTF-8-Byteoffsets. `preedit` akzeptiert nur geordnete
Grenzen innerhalb des Textes, die nicht mitten in einer Mehrbyte-Sequenz
liegen. `commit` normalisiert beide Grenzen auf das UTF-8-Byteende des
Textes; `cancel` normalisiert Text und Grenzen auf leer, 0 und 0. Die Revision
muss pro Ereignis eine positive ganze Zahl sein.

Der reine Wertkonstruktor verändert keinen Widget- oder Eingabestatus. Er
beweist weder monotone Revisionen über mehrere Aufrufe noch eine native
Windows-, GTK- oder Cocoa-IME-Brücke oder Plattformparität. Diese Aussagen
bleiben den zustandsbehafteten Kern- und echten Plattformgates vorbehalten.

## Vertrauensgrenzen

Native Ingress-Serials werden vom Server vergeben und müssen lückenlos
`accepted_serial + 1` sein. Direkte Clientnachrichten dürfen diese APIs nicht
aufrufen. `moo_input_client_create` ist ebenfalls eine Server-API: Nur der
Server weist Screenreader- oder Automation-Capabilities zu. Normale Clients
können weder den Semantikbaum auslesen noch A11y-Aktionen injizieren.

Der Rohpfad `moo_input_target_create_trusted` existiert nur für kontrollierte
In-Process-Tests. Produktionsadapter müssen
`moo_input_target_create_for_surface` benutzen; fremde oder stale
Compositor-Surfaces werden abgewiesen.

## Lebensdauer- und Queue-Invarianten

Handles enthalten Art, Generation und Slot. Alte oder fremde Handles sind
ungültig. Jedes Pointer-, Touch-, Key- oder Preedit-Down reserviert vor der
Zustandsmutation einen terminalen Release-/Cancel-Slot. Normale Events dürfen
diese Reserven nicht verbrauchen. Fokuswechsel emittieren zuerst synthetische
Key-Up- und IME-Cancel-Events, dann Blur/Focus. Ein fremder Client-Disconnect
verändert Fokus und Eingabestatus nicht.

UTF-8 wird längenbasiert geprüft. Overlong-Sequenzen, Surrogates,
Codepoints über U+10FFFF, NUL und Auswahlgrenzen mitten in einem Codepoint
werden abgewiesen. Eine Composition ist an `focus_epoch`, Target und
monotone Revision gebunden.

A11y-Parent- und Relationskanten müssen denselben Owner und dasselbe Target
haben. Jeder nichtleere Target-Baum besitzt genau eine Root; Zyklen, zweite
Roots und zu große Tiefe sind ungültig. Mehrknoten-Updates validieren
alle Revisionen und den vorgeschlagenen Parent-Overlay vor dem Commit.
Passwortwerte einschließlich ungenutzter Pufferbytes werden beim
Screenreader-Read immer redigiert und beim Destroy/Disconnect genullt.
Texttargets unterscheiden NONE, NORMAL und SENSITIVE. Normale Texte fließen in
den semantischen Statehash ein; sensitive Texte und Passwortwerte nicht.

## Capability-Ehrlichkeit

`MooInputConfig.features` enthält nur vom konkreten Adapter belegte
Fähigkeiten. Nicht gesetzte Features liefern `MOO_INPUT_UNSUPPORTED`.
Insbesondere bedeutet ein vorhandener Eventtyp nicht, dass GTK, Win32 oder
Cocoa ihn bereits nativ liefern. Core/Fake und der isolierte Win32-
Pointer/Keyboard/IMM-Adapter sind belegt. UIA/AT-SPI/NSAccessibility,
GTK/Cocoa-IME sowie native Touch-/Stift-Brücken werden erst nach ihrem
jeweiligen Plattformlauf als verfügbar markiert.

## Reproduzierbares Gate

`mise run test-ui-moo-input` führt aus:

- gezielte Vertrags-, Security-, UTF-8-, Queue-, Fokus-, IME-, Shortcut-,
  A11y-, Privacy- und Disconnect-Tests unter ASan und UBSan,
- die O3-Compositor-Bridge mit Owner-/Stale-Negativkontrollen,
- 20.000 deterministische Differentialschritte mit unabhängiger
  Cleanup-Reservationsinvariante,
- freestanding `-nostdlib`-Relink und `nm -u` ohne Hosted-Symbole.

Host-UI-Tests laufen niemals in einer unbekannten persönlichen Desktop-
Sitzung. Für die Windows-VM wird nach einem sauberen Commit ein verifiziertes
Quellarchiv übertragen; erwarteter Commit und SHA-256 werden vor jedem Lauf
protokolliert.

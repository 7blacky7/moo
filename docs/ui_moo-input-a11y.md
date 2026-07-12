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
Cocoa ihn bereits nativ liefern. Der Core-/Fake-Adapter ist belegt; native
IME-, UIA/AT-SPI/NSAccessibility- und Touch/Stift-Brücken werden erst nach
ihrem jeweiligen Plattformlauf als verfügbar markiert.

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

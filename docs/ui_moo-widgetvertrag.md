# ui_moo-Widgetvertrag v1

Status: P016-F1. Dieses Dokument und `docs/ui_moo-backendvertrag.md` sind kanonisch; `docs/ui_moo.md` und `docs/ui_moo-referenz.md` bleiben bis P016-D1 historische UIMOO-7-Artefakte. Der Backendvertrag steht in `docs/ui_moo-backendvertrag.md`. Dieses Dokument beschreibt die plattformneutrale retained-mode Schicht in `stdlib/ui_moo.moos`.

## Baum, Koordinaten und Z-Order

- Koordinaten eines Kindes sind relativ zum direkten Container.
- Panel- und Scroll-Transformationen dürfen beliebig verschachtelt werden.
- Geschwister werden stabil nach `z_index`, bei Gleichstand nach Einfügereihenfolge gezeichnet. Der Hit-Test verwendet exakt dieselbe Sortierfunktion in Gegenrichtung; der höchste sichtbare Eintrag gewinnt.
- Ein sichtbares, aber deaktiviertes Widget blockiert tiefere Z-Lagen. Es erhält selbst weder Fokus noch Aktivierung.
- Der Clip-Schalter ist ein gemeinsamer Zeichen- und Eingabevertrag. Standardmäßig clippen `scroll`, `panel`, `tabs`/interner `tab`, `expander`, `toolbar`, `teiler`, `karte`, `stapel`, `gitter` und `ueberlagerung`; freie/custom Widgets clippen nicht.
- `uim_clip_kinder_setze(w, wahr|falsch)` überschreibt diesen Standard explizit. Bei aktivem Clip sind außerhalb liegende Kinder weder sichtbar noch interaktiv; bei deaktiviertem Clip dürfen sie in beiden Pfaden über den Container hinausragen.

`uim_finde(kontext,id)` durchsucht den vollständigen Baum. `uim_hinzu(kontext,w)` invalidiert automatisch. Nach `uim_hinzu(container,w)` ist in v1 zusätzlich `uim_neuzeichnen(kontext)` aufzurufen; Widgets besitzen absichtlich keinen starken Rückverweis auf den Kontext.

## Fokus und Tastatur

Tab-Reihenfolge ist eine Tiefensuche in Einfügereihenfolge über tatsächlich sichtbare, aktive Unterbäume: nur der gewählte Tab, ein offener Aufklapper und ein offenes Overlay liefern Kinder. `uid` ist unveränderliche Identität. Ohne bisherigen Fokus wählt Tab das erste, Shift+Tab das letzte Ziel. Ein modales Overlay begrenzt die Reihenfolge auf seine Kinder und stellt beim Schließen den vorherigen noch interaktiven Fokus wieder her.

- Knopf/Checkbox: Return und Space aktivieren.
- Slider: Links/Rechts verändert den Wert in 1/20 der Spanne.
- Liste: Hoch/Runter verändert die Auswahl und scrollt die gewählte Zeile in den sichtbaren Bereich.
- Wird das fokussierte Widget oder einer seiner Vorfahren deaktiviert, verborgen oder aus dem Baum gelöst, mutiert ein Tastendruck das Widget nicht; der veraltete Fokus wird gelöscht.

## Auswahlmodus für Liste, Datengitter, Baum und Iconview

`liste`, `datengitter`, `baum` und `iconview` beginnen im Modus `einzel`.
`uim_auswahlmodus_setze(kontext,w,"einzel"|"mehrfach")` beziehungsweise
`uim_selection_mode_set` wechselt den Modus. `uim_optionen` und `uim_baue`
akzeptieren dafür `auswahl_modus`; der Modus wird vor einem zugleich gesetzten
`wert` angewandt. Bestehende Programme bleiben ohne Umschalten im Einzelmodus.
Dort haben Modifier keine zusätzliche Auswahlwirkung, Space toggelt die Auswahl
nicht und Return bleibt unverändert im allgemeinen Aktivierungsvertrag.

Maus und Tastatur folgen demselben Auswahlvertrag:

| Geste | `einzel` | `mehrfach` |
|---|---|---|
| Klick / Pfeil | Ziel ersetzt die Auswahl | Ziel ersetzt die Auswahl |
| Strg+Klick | Klick wie ohne Modifier | angeklicktes Ziel wird hinzugefügt oder entfernt |
| Space | allgemeine Aktivierung; Auswahl bleibt unverändert | fokussiertes Ziel wird hinzugefügt oder entfernt |
| Shift+Klick / Shift+Pfeil | wie ohne Modifier | geschlossener Bereich vom stabilen Anker zum Ziel ersetzt die Auswahl |
| Strg+Shift+Klick / Strg+Shift+Pfeil | wie ohne Modifier | Bereich vom Anker zum Ziel wird addiert |
| Strg+Pfeil | wie Pfeil ohne Modifier | nur Fokus und Sichtbarkeit ändern sich; Auswahl und Callback bleiben unverändert |
| Return | allgemeine Aktivierung, keine neue Auswahlsemantik | allgemeine Aktivierung, keine neue Auswahlsemantik |

Pfeile bewegen Liste, Datengitter und Baum zeilenweise; Iconview behält seine
zweidimensionale Rasterbewegung. Aus leerer Auswahl beginnt Abwärts/Rechts beim
ersten und Aufwärts/Links beim letzten Eintrag. Ziele werden an den Grenzen
geklemmt. Ein gültiger Auswahlgestus ruft `on_auswahl(w,index)` höchstens einmal
und erst nach vollständiger Zustandssynchronisierung auf; reine Strg-Navigation
ruft ihn nicht auf.

`uim_wert_hole(w)` liefert in `einzel` weiterhin einen Index oder `-1`. In
`mehrfach` liefert es stets eine frische, aufsteigend sortierte und duplikatfreie
Liste; die leere Auswahl ist `[]`. Entsprechend nimmt `uim_wert_setze` im
Einzelmodus einen Index und im Mehrfachmodus eine Liste. Programmatische Setter,
Moduswechsel und Datenreset lösen keinen Auswahl-Callback aus.

`auswahl_menge` ist für diese vier Typen die einzige Auswahlwahrheit. Die
Kompatibilitätsfelder `auswahl` und `selected` werden ausschließlich daraus
abgeleitet und dürfen von Aufrufern niemals direkt beschrieben werden.
Programmatische Auswahländerungen laufen über `uim_wert_setze`,
`uim_auswahlmodus_setze`/`uim_selection_mode_set`, `uim_optionen`, `uim_baue`
oder ein bidirektionales `uim_bind(...,"auswahl")` mit `uim_state_setze`; der
Bindingwert ist im Einzelmodus ein Index und im Mehrfachmodus eine Liste.
`uim_datengitter_zeilen_setze` leert die Auswahl beim Datenreset. Maus und
Tastatur verwenden dieselbe zentrale Auswahlwahrheit. `auswahl_anker` und
`auswahl_fokus` sind reiner Interaktionszustand und keine zweite
Auswahlwahrheit.

## Maus, Capture und Fokusverlust

Nur die Primärtaste gehört zum v1-Vertrag. Press und Release werden flankenbasiert normalisiert.

- Knopf/Checkbox aktivieren nur, wenn Press und Release dasselbe Widget treffen.
- Slider behält Capture außerhalb seines Rechtecks und klemmt auf min/max.
- Die rechte 8-Pixel-Zone eines scrollbaren `uim_scroll` gehört der Scrollbar. Thumb- und Track-Press starten Capture; Drag ist auch außerhalb möglich.
- Wird ein gecapturetes Widget oder einer seiner Vorfahren deaktiviert, verborgen oder aus dem Baum gelöst, endet Capture ohne weitere Wertänderung oder Aktivierung.
- Blur löst Druck und Hover. War die Polling-Maustaste beim Blur gehalten, werden weitere Press-Samples bis zum ersten echten Release verworfen; Refocus erzeugt keinen synthetischen Klick.

Positive Rad-Deltas verschieben den Viewport nach unten und den Inhalt nach oben. Scrollwerte werden auf den gültigen Bereich geklemmt.

## Theme und Resize

`uim_theme_setze` invalidiert und reconciled themeabhängige Scrollgrenzen. `uim_groesse_setze(kontext,b,h)` setzt die logische Root-Größe, klemmt negative Maße auf null und invalidiert. Frame und Mock bieten gleichnamige Komfortwrapper.

Themes sind in v1 vertrauenswürdige vollständige Dicts. Die öffentliche Schema-Versionierung und Validierung bleibt P016-D1; ein partielles Theme ist in v1 nicht konform.

## Capability-Grenzen

Das Leinwand- und Mock-Backend melden `clip=true`. Das vorhandene Frame-/Spielbackend meldet `clip=false`; dort ist visuelles Scissoring in v1 ausdrücklich nicht garantiert, während Hit-Testing weiterhin logisch clippt. Anwendungen, die visuelles Clipping zwingend benötigen, müssen die Capability prüfen.

## Deterministisches Gate

`beispiele/tests/ui_moo_contract_test.moos` prüft ohne Fenster oder Desktop-Toolkit:

- verschachtelte Offsets, Suche, Z-Order und Disabled-Blocker,
- logisches Clipping, Rad und draggable Scrollbar,
- Slider-Capture, Release außerhalb und Disable während Capture,
- rekursiven Tab-/Shift-Tab-Fokus und Listensichtbarkeit,
- Blur-/Refocus-Latch, Themewechsel, explizite Nested-Invalidierung und Resize,
- Clip-Kommandos eines `clip=true`-Backends.

Der Test muss mit `P016-F1-CONTRACT-OK` und Exitcode 0 enden. Host-UI-Tests gehören ausschließlich in die isolierte Plattformmatrix; unbekannte oder persönliche Desktop-Sitzungen bleiben fail closed.

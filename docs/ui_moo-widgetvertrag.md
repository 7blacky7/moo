# ui_moo-Widgetvertrag v1

Status: P016-F1. Dieses Dokument und `docs/ui_moo-backendvertrag.md` sind kanonisch; `docs/ui_moo.md` und `docs/ui_moo-referenz.md` bleiben bis P016-D1 historische UIMOO-7-Artefakte. Der Backendvertrag steht in `docs/ui_moo-backendvertrag.md`. Dieses Dokument beschreibt die plattformneutrale retained-mode Schicht in `stdlib/ui_moo.moos`.

## Baum, Koordinaten und Z-Order

- Koordinaten eines Kindes sind relativ zum direkten Container.
- Panel- und Scroll-Transformationen dürfen beliebig verschachtelt werden.
- Geschwister werden in Einfügereihenfolge gezeichnet; beim Hit-Test gewinnt das zuletzt hinzugefügte sichtbare Widget.
- Ein sichtbares, aber deaktiviertes Widget blockiert tiefere Z-Lagen. Es erhält selbst weder Fokus noch Aktivierung.
- Scrollbereiche begrenzen Hit-Tests immer auf ihr Rechteck. Backends mit `clip=true` begrenzen dort auch das Zeichnen.
- Panels clippen ihre Kinder in v1 bewusst nicht. Panel-Overflow kann sichtbar sein, ist außerhalb des Panel-Rechtecks aber nicht interaktiv.

`uim_finde(kontext,id)` durchsucht den vollständigen Baum. `uim_hinzu(kontext,w)` invalidiert automatisch. Nach `uim_hinzu(container,w)` ist in v1 zusätzlich `uim_neuzeichnen(kontext)` aufzurufen; Widgets besitzen absichtlich keinen starken Rückverweis auf den Kontext.

## Fokus und Tastatur

Tab-Reihenfolge ist eine Tiefensuche in Einfügereihenfolge über alle sichtbaren, aktiven Unterbäume. `uid` ist ein reserviertes, nach der Konstruktion unveränderliches Identitätsfeld; Anwendungszustand darf es nicht überschreiben. Ohne bisherigen Fokus wählt Tab das erste, Shift+Tab das letzte Ziel. Deaktivierte oder unsichtbare Ziele werden übersprungen.

- Knopf/Checkbox: Return und Space aktivieren.
- Slider: Links/Rechts verändert den Wert in 1/20 der Spanne.
- Liste: Hoch/Runter verändert die Auswahl und scrollt die gewählte Zeile in den sichtbaren Bereich.
- Wird das fokussierte Widget oder einer seiner Vorfahren deaktiviert, verborgen oder aus dem Baum gelöst, mutiert ein Tastendruck das Widget nicht; der veraltete Fokus wird gelöscht.

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

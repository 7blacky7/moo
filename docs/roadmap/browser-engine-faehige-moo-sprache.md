# Browser-Engine-faehige moo-Sprache

Stand: 2026-07-05  
Status: **Roadmap / spaeterer Plan, noch nicht umsetzen**

## Warum diese Datei existiert

Der KI-Pfad hat aktuell Prioritaet. Der Windows-Pfad wurde vorerst angehalten. Nach Abschluss des KI-Pfads soll geprueft werden, wie `moo` so weiterentwickelt werden kann, dass langfristig eine eigene Browser-Engine moeglich wird.

Diese Datei ist bewusst als Markdown-Datei abgelegt und **nicht** als Memory/Thought, damit spaetere KI-Agenten sie gezielt lesen koennen, ohne den aktuellen Arbeitskontext unnoetig zu belasten.

## Zielbild

`moo` soll langfristig nicht nur eine Browser-Shell mit eingebettetem WebView bauen koennen, sondern auch die Grundlage fuer eine eigene Engine liefern:

- eigene Dokument-/UI-Engine
- spaeter HTML/CSS-Subset
- spaeter DOM, Events, Layout und Rendering
- spaeter Script-Anbindung
- langfristig: genug Fundament, um echte Browser-Engine-Forschung im Projekt zu betreiben

Wichtig: Das Ziel ist **nicht**, sofort Chrome/Firefox nachzubauen. Das waere als Monolith zu gross. Ziel ist ein stufenweiser Engine-Pfad mit kleinen, testbaren Gates.

## Grundsatzentscheidung

Keine riesige Browser-Engine auf einmal bauen.

Stattdessen die Sprache und Runtime so gestalten, dass die Bausteine fuer eine eigene Engine sauber entstehen:

- schnelle Strings, Bytes und Streams
- Parser-freundliche APIs
- effiziente Baumstrukturen fuer DOM-/Layout-Baeume
- 2D-Rendering/Canvas/Compositing
- async IO und Netzwerk
- optional GPU-Beschleunigung
- saubere Fehler- und Testinfrastruktur
- spaeter Sandbox-/Permission-Modell

## Nicht-Ziel fuer den ersten Schritt

Nicht sofort:

- vollstaendiger HTML5-Parser
- vollstaendiges CSS
- JavaScript-Kompatibilitaet
- WebGL/WebGPU
- Video/Audio-Stack
- Extensions
- kompletter Security-Sandbox

Diese Themen gehoeren spaeter in eigene Phasen.

## Phase BE-0: Ist-Zustand erfassen

Nach dem KI-Pfad zuerst mit `code_intel` pruefen:

- Welche UI-Funktionen sind stabil?
- Welche 2D-/Canvas-Funktionen existieren?
- Welche Netzwerk-/HTTP-/WebSocket-Funktionen existieren?
- Welche Parser-/String-/Bytes-Funktionen sind schnell genug?
- Welche Windows/Linux/macOS-Pfade sind wirklich getestet?
- Welche Teile sind nur Demo, welche produktionsnah?

Ergebnis von BE-0 soll eine kurze Bestandsaufnahme sein, nicht direkt Code.

## Phase BE-1: Eigene Dokument-Engine, kein Web

Ziel: Eine eigene kleine Dokument-Engine fuer `moo`, ohne HTML/CSS-Kompatibilitaetsdruck.

Moegliche Bausteine:

- `Dokument`
- `Knoten`
- `TextKnoten`
- `Box`
- `Style`
- einfacher Layoutbaum
- Text, Flaechen, Rahmen, Farben
- Maus-/Tastatur-Events
- Scrollbereich

Gate:

- Eine einfache Dokumentseite wird aus einer Baumstruktur gerendert.
- Layout ist deterministisch testbar.
- Screenshot-/Snapshot-Test existiert.

## Phase BE-2: Markup-Subset

Ziel: Ein eigenes kleines Markup oder ein HTML-aehnliches Subset parsen.

Nicht direkt HTML5 vollstaendig. Erst kontrolliert:

- Tags
- Attribute
- Text
- einfache Verschachtelung
- Fehler tolerant behandeln, aber dokumentiert

Gate:

- Parser erzeugt Baum.
- Baum wird von BE-1 gerendert.
- Fehlerfaelle haben klare deutsche Meldungen.

## Phase BE-3: CSS-Subset

Ziel: Kleines CSS-Subset.

Startumfang:

- Farben
- Schriftgroessen
- margin/padding
- border
- display block/inline-ähnlich
- einfache Selektoren: Tag, Klasse, ID

Noch kein Flexbox/Grid.

Gate:

- CSS-Regeln werden geparst.
- Styles werden korrekt auf Knoten angewendet.
- Snapshot-Test vergleicht Layout/Rendering.

## Phase BE-4: Layout-Engine

Ziel: Eigene Layout-Engine.

Start:

- Block-Layout
- Inline-Text-Layout einfach
- Zeilenumbruch
- Scrollen
- Reflow bei Fensterbreite

Spaeter:

- Flexbox
- Grid
- komplexe Textmessung

Gate:

- feste Testdokumente liefern reproduzierbare Box-Positionen.
- Rendering ist plattformuebergreifend nah genug oder Abweichungen sind dokumentiert.

## Phase BE-5: DOM und Events

Ziel: Interaktiver Baum.

Start:

- Node-API
- Element-API
- Attribute
- query nach ID/Klasse/Tag
- Event-Bubbling minimal
- click/input/key events

Gate:

- Button veraendert Text im Dokument.
- Input-Feld aktualisiert Baumzustand.
- Event-Reihenfolge ist getestet.

## Phase BE-6: Script-Schicht

Ziel: Erst `moo` als Script-Sprache fuer die Engine, nicht sofort JavaScript.

Moegliche Richtung:

- Dokument laedt `.moos`-Scripte.
- Script kann DOM lesen/aendern.
- Script kann Events registrieren.

JavaScript-Kompatibilitaet ist ein spaeter eigener Mega-Task.

Gate:

- Eine kleine Seite wird per moo-Script interaktiv.
- Scriptfehler crashen nicht die Engine.

## Phase BE-7: Netzwerk und Ressourcen

Ziel: Ressourcen laden.

Start:

- lokale Dateien
- HTTP GET
- Bilder falls bereits Runtime-Unterstuetzung vorhanden
- Fonts spaeter
- Cache spaeter

Gate:

- Dokument kann externe Ressourcen laden.
- Fehler/Timeouts sind sauber sichtbar.

## Phase BE-8: Browser-Shell um die Engine

Erst wenn eigene Dokument-Engine stabil ist:

- Tabs
- Adressleiste
- Verlauf
- Reload
- Zurueck/Vorwaerts
- Entwickler-/Debugansicht
- Ressourcen-/DOM-Inspektor spaeter

Gate:

- Mehrere Dokumente in Tabs.
- Navigation zwischen lokalen und einfachen HTTP-Dokumenten.

## Sprach-/Runtime-Anforderungen

Damit das realistisch wird, sollte `moo` besonders stark werden in:

1. **Parserbau**
   - schnelle Byte-/String-Funktionen
   - Slice/Substring ohne zu viele Kopien, falls moeglich
   - gute Fehlermeldungen mit Zeile/Spalte

2. **Baumstrukturen**
   - effiziente Listen/Dicts oder eigener Node-Typ
   - Refcount-Regeln klar dokumentieren
   - keine versteckten Leaks bei vielen kleinen Objekten

3. **Rendering**
   - 2D-Primitiven
   - Textmessung
   - Bilder
   - Clipping
   - Offscreen-Buffer/Snapshot

4. **Events**
   - Maus/Tastatur
   - Fokus
   - Timer
   - async Tasks spaeter

5. **Netzwerk**
   - HTTP Client stabil
   - TLS/HTTPS klaeren
   - Cookies/Headers/Redirects spaeter

6. **Tests**
   - Parser-Fixtures
   - Layout-Golden-Tests
   - Snapshot/Pixel-Tests
   - deterministische Event-Tests

## Wichtige Designregel

Die Engine soll in kleinen Schichten entstehen:

```text
Bytes/String/Parser
        ↓
Dokumentbaum
        ↓
Style-System
        ↓
Layoutbaum
        ↓
Painting/Canvas
        ↓
Events/DOM
        ↓
Script
        ↓
Browser-Shell
```

Keine Schicht darf heimlich mehrere spaetere Schichten simulieren.

## Empfehlung fuer spaetere KI-Agenten

Wenn diese Datei spaeter gelesen wird:

1. Nicht direkt anfangen zu coden.
2. Erst mit `code_intel` aktuellen Stand von UI, Grafik, Netzwerk, Bytes, Parsern und Tests pruefen.
3. Danach BE-0 als Bestandsaufnahme schreiben.
4. Dann maximal BE-1 als kleines, testbares Ziel starten.

## Kurzfazit

`moo` soll langfristig so wachsen, dass eine eigene Browser-Engine moeglich wird. Der Weg dorthin ist nicht „Browser komplett bauen“, sondern erst eine eigene Dokument-/Layout-/Rendering-Engine als kontrolliertes Fundament. Daraus kann spaeter ein echter Browser entstehen.

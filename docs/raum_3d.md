# 3D mit `raum_*` — Welten bauen in moo

Referenz fuer das GL33-Backend (Standard-Feature `gl33`). Alle Funktionen nehmen
als erstes Argument das Fenster (`raum_erstelle` oder `hybrid_open`).
Zustaende wie Licht, Farbton, Textur, Leuchten oder Mischmodus sind
**Draw-States**: Sie gelten fuer alle folgenden Zeichenaufrufe, bis sie neu
gesetzt werden.

Stand: 2026-09-22 (EPIC 3D-WELT, Tasks ERDE-1 bis ERDE-16, Licht-Stufe 3/4).
Beispiel: `beispiele/raum_welt_features.moos`.

## Fenster schliessen

`raum_schliessen(w)` schliesst nur das aktive, vom reinen 3D-Pfad verwaltete
Fenster. Fuer Hybrid-Fenster sowie nicht aktive oder bereits geschlossene
Handles ist dieser Aufruf ein No-op. Insbesondere schliesst ein alter
3D-Handle kein inzwischen aktives Hybrid-Fenster. Hybrid-Fenster werden
mit `hybrid_schliessen(w)` geschlossen. Dies ist die seit dem Lifecycle-Guard
praezisierte Besitzregel; der Dispatcher verwaltet weiterhin nur einen
aktiven Renderingkontext.

## Renderer-Metadaten ohne Capture

`test_renderer_info(win)` liefert ein Dictionary mit `available` (Bool) sowie
`backend`, `api`, `vendor`, `renderer`, `version` (kopierte Strings).
GL33 liefert `backend="gl33"`, `api="OpenGL"` und die echten
GL_VENDOR/GL_RENDERER/GL_VERSION des aktiven reinen 3D- oder Hybrid-Kontextes.
`version` ist der GL-Versionsstring, kein separat geparster Treiberversionsbeleg.
Die Abfrage erzeugt kein Fenster, liest keine Pixel, fuehrt keine Postpasses aus
und veraendert weder Renderzeit noch Framezustand. Ein zuvor aktueller anderer
GLFW-Kontext wird wiederhergestellt.

Ein gueltiger aktiver Backendkontext ohne Metadatenhook (derzeit GL21/Vulkan)
liefert `available=false` und `"unknown"` fuer die Stringfelder.
Fremde Typen (auch SDL2-Fenster), geschlossene oder nicht aktive Handles werfen
einen Fehler. Der Dispatcher unterstuetzt weiterhin nur einen aktiven Kontext;
Pointer-Handles bieten keine Generationsgarantie bei Wiederverwendung einer
Adresse nach dem Schliessen. Die zurueckgegebenen Strings bleiben nach Close gueltig.
`test_fenster_info` bleibt unveraendert und kann einen Capture ausloesen;
fuer Provenienz vor dem ersten Draw daher `test_renderer_info` verwenden.

Opt-in Tests: `mise run test-gfx-renderinfo` und `mise run test-gfx-lifecycle`.
Privates Xvfb/llvmpipe prueft Korrektheit, keine GPU-Performance.

## Koordinaten, Kamera, Transform

| Aufruf | Wirkung |
|---|---|
| `raum_perspektive(w, fov, nah, fern)` | Projektion |
| `raum_kamera(w, ex, ey, ez, lx, ly, lz)` | Kamera (View-Matrix). Das Model startet danach bei Identitaet. Der Blickpunkt `l` ist auch das Zentrum des Schattenbereichs. |
| `raum_push(w)` / `raum_pop(w)` | Model-Matrix sichern/zuruecksetzen |
| `raum_verschiebe(w, x, y, z)`, `raum_rotiere(w, grad, ax, ay, az)` | Model-Transform |
| `raum_skaliere(w, s)` / `raum_skaliere(w, sx, sy, sz)` | Skalierung; Normalen bleiben auch bei ungleichmaessiger Skalierung korrekt |

Licht, Normalen und Nebel rechnen im **Weltraum** (seit ERDE-1): Das Licht dreht
sich nicht mehr mit der Kamera.

## Licht

| Aufruf | Wirkung |
|---|---|
| `raum_licht(w, x, y, z)` | Richtung **zum** Hauptlicht |
| `raum_lichtfarbe(w, farbe)` | Farbe des Hauptlichts |
| `raum_umgebungslicht(w, 0..1)` | Grundhelligkeit |
| `raum_licht_hinzufuegen(w, idx, x, y, z, farbe)` | idx 0 = Hauptlicht, 1..3 = weitere Richtungslichter (zweite Sonne, Mond). Farbe `"#000000"` schaltet aus. |
| `raum_punktlicht(w, idx, x, y, z, radius, farbe)` | idx 0..3, Weltkoordinaten, weicher Abfall bis `radius`; radius 0 = aus |
| `raum_glanz(w, staerke, haerte)` | Blinn-Phong-Glanz (0 = aus) |
| `raum_schatten(w, an[, aufloesung=2048[, bereich=60]])` | Shadow-Map des Hauptlichts mit 3x3-PCF |

**Schatten:** Nur `chunk_zeichne` wirft Schatten, alle Draws empfangen ihn. Die
Map entsteht bei `raum_aktualisieren`/`hybrid_aktualisieren` und gilt ab dem
**naechsten** Frame. `bereich` ist die halbe Kantenlaenge des Bereichs um den
Kamera-Blickpunkt: kleiner = schaerfer.

## Material

| Aufruf | Wirkung |
|---|---|
| `raum_farbton(w, farbe)` | multipliziert die Farbe folgender Draws (Tint pro Instanz), `"#FFFFFF"` = aus |
| `raum_leuchten(w, staerke)` | Eigenleuchten: addiert Grundfarbe x Staerke, unabhaengig vom Licht (0 = aus) |
| `raum_mischmodus(w, "additiv" \| "normal")` | additiv fuer Glow, Partikel, Blitze; schreibt keine Tiefe |
| `raum_transparenz(w, 0..1)` | Alpha folgender Draws |
| `raum_glaetten(w, wahr \| falsch)` | Folgende `chunk_ende` mitteln Normalen gemeinsamer Ecken (weiches Terrain). Knicke ueber 60 Grad bleiben scharf. |
| `raum_wellen(w, amp, freq, tempo)` | Wasser-/Graswellen, laufen auch im Hybrid-Fenster |

Hinweis zu additiv: Auf hellem Hintergrund (Tageshimmel) saettigt additiv schnell
zu Weiss. Fuer farbige Leuchtobjekte bei Tag besser `normal` + `raum_leuchten`.

## Reproduzierbare Renderzeit (GL33 und Hybrid)

`raum_renderzeit(w, sekunden)` (Alias `space_render_time`) setzt die absolute
Wellen-/Renderzeit sofort fuer folgende Draws, auch vor dem ersten Frame.
Die Zeit muss eine endliche, nichtnegative Zahl bis `FLT_MAX` sein und wird
auf die Genauigkeit der float-Shaderuniform gerundet. Sehr grosse Zeiten verlieren
Phasenpraezision; Zeit mal Wellengeschwindigkeit kann trotz gueltiger Zeit die
Shader-Reichweite ueberschreiten. Die API garantiert dafuer kein sinnvolles Bild.
Weltzeit, Sonnenstand
und Simulations-`dt` bleiben davon unabhaengig.

`raum_renderzeit_live(w)` (Alias `space_render_time_live`) laedt sofort die
aktuelle absolute GLFW-Zeit. Das ist ein bewusster Zeitsprung ohne
Pausenkompensation; danach gilt wieder der bisherige Endframe-Zeittakt.
Ohne diese Aufrufe bleibt der Live-Default unveraendert. Wie andere
`raum_*`-Aufrufe wirken beide auf den aktiven 3D-Kontext.
GL21, Vulkan und ein fehlender aktiver Kontext melden einen Fehler,
ebenso ungueltige Zeitwerte; es gibt keinen stillen Fallback.

Im Fixmodus aendern Capture und Present die Zeit nicht. Fuer statische
Baselines die Zeit vor Draw setzen, Frame0 getrennt von Warmup-Frames
erfassen und nach dem finalen Draw zweimal ohne Zwischendraw capturen.
Capture enthaelt den bestehenden Post-Pass vor Swap; danach bis zum neuen
Frame nicht weiterzeichnen. Fuer Sequenzen die Zeit explizit je Frame setzen.
Schliessen und Neuerstellen des Kontexts startet wieder im Live-Default.

## Texturen

| Aufruf | Wirkung |
|---|---|
| `raum_textur_laden(w, pfad)` | PNG/JPG/BMP -> Textur-ID, bei Fehler `nichts` |
| `raum_textur_aus_flaeche(w, surface)` | prozedurale Textur aus `surface_new`/`surface_rect`/`surface_text` |
| `raum_textur(w, id[, skala=1])` | aktive Textur, 0 = aus |

Texturen werden **triplanar in Weltkoordinaten** gemappt: Es braucht keine
UVs, sie wirken auch auf bestehende Chunks. `skala` = Wiederholungen pro
Welteinheit. Die Textur multipliziert die Vertex-Farbe (weisse Geometrie zeigt
die Textur unveraendert).

## Linearer HDR-Modus (GL33, opt-in)

`raum_hdr(w, wahr)` / `space_hdr` muss vor dem ersten Szenenframe,
Chunk- oder Texturaufbau stehen. Danach bleibt der Modus fuer dieses Fenster
fest. Ohne Opt-in bleibt die bisherige Ausgabe erhalten. GL21/Vulkan melden
fuer diese API einen Fehler statt still auf einen anderen Renderer zu wechseln.

`raum_belichtung(w, multiplikator)` / `space_exposure` nimmt endliche Werte
von 0 bis 64 an; Standard ist 1. Der Aufruf schaltet automatische Belichtung aus.
`raum_auto_belichtung(w, min, max, key, tau)` / `space_auto_exposure`
aktiviert Auto-Exposure: endliche Werte mit `0 < min <= max <= 64`,
`key > 0` und `tau >= 0` (Sekunden). Jeder Aufruf setzt die Anpassung zurueck;
deshalb nur bei Aktivierung oder Parameterwechsel aufrufen.
`raum_belichtung_wert(w)` / `space_exposure_value` liest den gespeicherten
Multiplikator ohne Rendern oder Anpassung. Vor dem ersten Auto-Frame liefert
er die bisherige manuelle Belichtung.

Gemessen wird vor Bloom das geometrische Mittel
`exp(mean(log(max(dot(linearRGB, [.2126,.7152,.0722]), 1e-6))))`.
Die GPU reduziert gueltige Pixel mittels RG32F-Summen und Pixelzahlen, auch
bei ungeraden Bildmassen. Nur das letzte 1x1-Ergebnis wird synchron gelesen;
dieser Readback ist ein bewusster Synchronisationspunkt.
Texel mit NaN/Inf in RGB werden aus Summe und Pixelzahl ausgeschlossen.
Ohne gueltige Texel oder bei Messressourcenfehlern bleibt die bisherige
Belichtung erhalten. SceneEnd beendet zuerst Bloom/Tonemapping und stellt
den GL-Zustand wieder her, danach meldet es den Messfehler. Nach dem Abfangen
sind Capture und wiederholtes SceneEnd sicher; der naechste Frame versucht
die Messung erneut. Der Filter repariert keine ungueltigen Szenenfarben.
Ziel ist `clamp(key / luminanzmittel, min, max)`; im ersten Frame, nach
Resize oder rueckwaerts laufender Renderzeit wird es direkt uebernommen.
Weitere Frames glaetten im **linearen Exposure-Raum** mit
`E += (Ziel-E) * (1-exp(-dt/tau))`. Bei positivem dt und tau=0 gilt das Ziel
sofort, dt=0 laesst die Belichtung unveraendert. Fixe Renderzeit bestimmt
auch dt; ein Wechsel zwischen fixer und Live-Zeit setzt die Anpassung zurueck.
SceneEnd passt hoechstens einmal pro Frame an. Wiederholtes Capture, Getter
oder Present fuehrt keine weitere Messung/Anpassung aus.
Baselines muessen Auto-Parameter, Renderzeit und den nach SceneEnd verwendeten
Multiplikator dokumentieren.
`raum_szene_ende(w)` / `space_scene_end` gehoert nach der transparenten Welt
und vor das HUD, auch wenn das HUD ausgeschaltet ist. Wiederholte Aufrufe sind
wirkungslos. Capture/Present schliessen eine noch offene Szene automatisch ab;
Capture bewegt weder Renderzeit noch den Schattenpass vorwaerts. Nach Abschluss
sind weitere Szenen-Draws bis zum naechsten Clear ein Fehler.

Die Szene und Bloom arbeiten auf linearen RGBA16F-Zielen mit eigener
DEPTH_COMPONENT32F-Tiefe. Farbwerte fuer Vertex, Textur, Tint, Licht, Himmel,
Nebel und Clear werden einmal aus sRGB dekodiert (ausser explizit linearen
Lichtslots, siehe unten). Die bisherigen Lichtfarbsetter interpretieren auch
Komponenten groesser als 1 als sRGB-codierte Farbwerte: Die aktuelle
Transferfunktion wird ohne vorgelagerten 0..1-Clamp fortgesetzt
(z.B. 2.0 wird ungefaehr 4.95 linear). Solche Werte sind keine linearen
Intensitaetsmultiplikatoren. Fuer lineare Lichtkoeffizienten einschliesslich
Werten ueber 1 ist `raum_licht_linear` / `space_light_linear` vorgesehen.
Ambient, Emissionsstaerke und Exposure sind dagegen Skalare ohne sRGB-Decode;
auch Alpha, Distanzen und Normalen werden nicht sRGB-dekodiert.
Texturen werden vor Filterung dekodiert,
Vertexfarben vor Interpolation. Der Ausgabepass nutzt Stephen Hills
**ACES Fitted** (BakingLab, MIT), anschliessend genau einmal die sRGB-Kennlinie.
Dies ist eine ACES-Approximation, keine vollstaendige ACES-Pipeline.
Vor dem Fit wird Szenen-RGB auf 0..65504 begrenzt und mit Exposure multipliziert.
Bei Exposure 1 wird lineares Grau 1 ungefaehr als sRGB 0.809 ausgegeben.

Das Hybrid-HUD wird danach mit seinen bisherigen Farbcodewerten und
SRC_ALPHA-Blending gezeichnet. Seine finale Tiefe wird genau einmal pro Szene
neu initialisiert; HUD-Z, LEQUAL und Depth-Write bleiben erhalten.
Wiederholtes Capture loescht diese Tiefe nicht. Szenenziele verwenden
Framebuffer-Pixelmasse. Ein minimiertes Fenster ohne Pixel meldet beim
HDR-Clear einen Fehler; nach Wiederherstellung legt Clear passende Ziele an.
Bekannte GLX-Grenze: direkt nach programmatischer Vergroesserung kann der
erste Capture vor einer normalen Presentation noch alte BACK-Puffergrenzen
zeigen. Der getestete Resize-Pfad prueft den neu gerenderten Frame nach
Presentation; die Engine fuegt keinen versteckten Swap hinzu.
Separater Reproducer: `GFX_HDR_RESIZE_BEFORE_PRESENT=1 mise run test-gfx-hdr-targets`.

`raum_licht_linear(w, slot, r, g, b)` / `space_light_linear` setzt im HDR-Modus
ausdruecklich lineare Lichtkoeffizienten (endliche RGB-Werte 0..65504):
Slot 0 ist das Hauptlicht, 1..3 sind weitere Richtungslichter, 4..7 die
Punktlichter 0..3. Richtung, Position und Radius bleiben unveraendert.
Bestehende Farbsetter stellen den jeweiligen Slot wieder auf ihren bisherigen
sRGB-Farbmodus. Haupt-/Zusatz-/Punktlicht, Glanz und Sonnennebel verwenden
denselben Modus. Intensitaetsgewichte werden im linearen Raum multipliziert;
neue sRGB-Farbkonstanten zuerst dekodieren, dann gewichten. Der explizite
ERDE-Legacyadapter behandelt bisher empirisch lineare Lichtkoeffizienten als
solche; Albedo, Tint und der lineare Ambient-Skalar werden dadurch nicht geaendert.

## Umgebung und Nachbearbeitung

| Aufruf | Wirkung |
|---|---|
| `raum_himmel(w, zenit, horizont)` | Himmelsverlauf, unbeleuchtet (unabhaengig von Licht/Nebel) |
| `raum_nebel(w, dichte)` | Nebel pro Welteinheit (z.B. 0.002 bis 0.05); ohne Aufruf kein Nebel |
| `raum_nebel_farbe(w, farbe)` | Nebelfarbe |
| `raum_bloom(w, staerke[, schwelle=0.8[, radius=2]])` | Helle Bildteile strahlen weich aus (Post-Pass, 0 = aus). `radius` 1..8 = Blur-Durchgaenge, groesser = weiterer Hof. Alias `raum_gluehen`. |

## Nebel: Dichte null und optionales Hoehenprofil

`raum_nebel(w, dichte)` akzeptiert endliche Werte von 0 bis 1000000.
Die vorhandene Basis-API wirkt weiterhin auf den aktiven globalen 3D-Kontext;
ihr Fensterargument ist keine Handle-Validierung. Ohne aktiven Kontext bleibt
sie wirkungslos. Negative und nichtnumerische Dichten sind Fehler.

| Backend | Verhalten bei Dichte 0 | Hoehenprofil |
|---|---|---|
| GL33, ohne HDR | Exakt aus durch expliziten Shader-Nullzweig; keine nur grosse Sichtweite | Nicht unterstuetzt |
| GL33, HDR; auch Hybrid | Exakt aus, auch bei aktivem Hoehenprofil | Unterstuetzt |
| GL21 | `GL_EXP` mit `GL_FOG_DENSITY=0`: aus bei endlicher Distanz; Quell-/API-Beleg, hier kein neues GPU-Gate | Nicht unterstuetzt |
| Vulkan | **Bekannte Ausnahme:** bestehender Setter begrenzt die Dichte weiterhin auf mindestens 0.001; Dichte 0 schaltet deshalb nicht aus. Der eingebettete SPIR-V-Pfad ist in diesem Schritt unveraendert. | Nicht unterstuetzt |

Die Vulkan-Ausnahme ist keine Backend-Paritaetszusage und kein stiller Fallback.
Bei kleinen positiven Dichten unter 0.001 behaelt GL33 ohne HDR ebenfalls
seine bisherige Mindestdichte; im HDR-Pfad bleiben diese Werte ungeklammert.

`raum_hoehennebel(w, hoehe, falloff, sonne)` / `space_height_fog` aktiviert
das analytische Profil im offenen HDR-Szenenframe. Zulaessig sind endliche
Werte mit Hoehe in +/-1e7, Falloff in 0..1000 und Sonnenanteil in 0..1.
`raum_hoehennebel_aus(w)` / `space_height_fog_off` schaltet zur bisherigen
Distanz-/Hoehenformel zurueck; fuer keinerlei Nebel zusaetzlich Dichte 0 setzen.
Diese neuen Aufrufe validieren das aktive Fenster und melden bei GL21/Vulkan
einen Fehler statt einer stillen Emulation.

Die Dichte entlang des Sichtstrahls lautet
`rho(y)=dichte*exp(-falloff*(y-hoehe))`; integriert wird analytisch zwischen
Kamera und Fragment. Additive Emission wird nur mit der Transmission T
abgeschwaecht. Andere Geometrie erhaelt `L*T + S*(1-T)`.
Der Sonnenanteil in S ist eine begrenzte kuenstlerische Richtungslobe mit
derselben linearen Hauptlichtfarbe wie die Beleuchtung, keine vollstaendige
Mehrfachstreuung. Der numerische Arbeitsbereich begrenzt Hoehen auf +/-1e7,
Distanz auf 1e8 und optische Tiefe auf 80.
Unlit-Himmel und Overlay werden nicht vernebelt. Der separate Wasserzweig
ueberspringt diesen Luftnebel; eine kombinierte Luft-/Wassertransportloesung
ist noch nicht implementiert.

Korrektheitsgate: `mise run test-gfx-fog` (CPU-Quadratur/Sanitizer und
native GL33-/Hybrid-Pixeltests mit und ohne HDR; privates Xvfb, keine
Performanceaussage).

## GGX-Material (GL33 mit HDR)

`raum_pbr(w, metallisch, rauheit)` / `space_pbr` aktiviert ein Material
fuer nachfolgende beleuchtete Draw-Aufrufe. Beide Werte muessen endlich in
0..1 liegen; Rauheit 0..0.05 verwendet die Untergrenze 0.05.
`raum_pbr_aus(w)` / `space_pbr_off` stellt die bisherige Beleuchtung wieder her.
PBR ist standardmaessig aus, erfordert HDR und ein aktives Fenster.
Wird HDR vor dem ersten Ressourcenaufbau wieder ausgeschaltet, setzt dies
auch PBR zurueck; erneutes HDR-Einschalten aktiviert PBR nicht automatisch.
GL21/Vulkan melden fuer diese neue API einen Fehler. Ungueltige Parameter
veraendern den vorherigen Materialzustand nicht.

Der Zustand gehoert zum Kontext und bleibt bis zum naechsten Aufruf erhalten;
er wird nicht in Chunks gespeichert. Vor dem Zeichnen jedes Materials setzen.
Wasser, unbeleuchtete Primitive und Overlay verwenden eigene Shaderpfade.

Das Direktlicht nutzt GGX mit alpha=Rauheit², exaktem separablem Smith-Term
und Schlick-Fresnel. Die diffuse Komponente wird durch Fresnel in Licht- und
Blickrichtung reduziert. Alle acht Lichtslots wirken; der explizite Adapter
von bisherigen linearen Lichtkoeffizienten lautet Li=pi*Farbe.
Haupt-/Zusatzrichtungslicht behalten den bisherigen Faktor (1-Ambient),
Punktlicht seine quadratische Reichweitenabschwächung und das Hauptlicht
seine Schatten. Der Ambient-Ersatz ist neutral
(1-Metallisch)*lineareBasisfarbe*Ambient; er verwendet nicht die Hauptlichtfarbe.
Es gibt hier noch keine IBL, indirekte Spiegelung oder Mehrfachstreuung:
Metalle ohne direkt sichtbaren Lichtreflex koennen daher schwarz erscheinen.
Die direkte BRDF ist einseitig: bei NoV<=0 bleibt nur der Ambient-Ersatz;
eine zweiseitige Materialbeleuchtung ist nicht Teil dieser API.
Emission und Nebel werden nach der Materialbeleuchtung einmal angewandt.

Der BRDF-Ausgang wird nicht auf 0..1 geklemmt. Das Szenenziel RGBA16F hat
jedoch einen maximalen endlichen Wert von 65504 pro Kanal. Eine gueltige
Lichtfarbe garantiert daher keine darstellbare akkumulierte Radiance:
bei frontalem weissem Metall und Rauheit 0.05 entsteht 40000*Lichtkoeffizient;
Koeffizient 2 ergibt mathematisch 80000 und ueberschreitet das Ziel.
Belichtung wirkt erst spaeter und kann diesen Speicherverlust nicht reparieren.
Auch darstellbare Radiance kann nach Tonemapping als 255 ausgegeben werden;
enge Reflexspitzen sind deshalb getrennt von grossflaechigem Clipping zu bewerten.

Korrektheitsgates: `mise run test-gfx-pbr-targets` prueft den produktiven
GLSL-Helper gegen unabhaengige RGB-/Weiss-Furnace-Oracles sowie echte Draws;
`mise run test-gfx-pbr` prueft die oeffentliche API in 3D und Hybrid.
Privates Xvfb/llvmpipe liefert dabei keine Performanceaussage.

## Raeumliche Umgebungsverdeckung (AO, GL33 mit HDR)

`raum_ao(w, radius, staerke, bias)` / `space_ao` aktiviert eine
deterministische, bildraumbasierte Naeherung fuer Umgebungsverdeckung.
Radius und Bias sind Weltlaengen: Radius 0..10000, Staerke 0..1 und
Bias 0..Radius; alle Werte muessen endlich sein.
`raum_ao_aus(w)` / `space_ao_off` schaltet AO aus.
Alle AO-Aufrufe verlangen ein aktives HDR-Fenster und einen unterstuetzten
Backend-Callback; GL21/Vulkan melden einen Fehler. Fehlerhafte Parameter
lassen die bisherige Konfiguration unveraendert.

Konfiguration und Debug-Schalter sind nur vor dem ersten Clear oder nach
abgeschlossenem SceneEnd erlaubt. Radius 0 oder Staerke 0 ergibt exakt die
bisherige Farbe; die konfigurierte AO-Phase bleibt trotzdem verbindlich:
Clear, deckende Welt, `raum_opak_ende(w)` / `space_opaque_end`,
transparente Welt/Wasser, SceneEnd, HUD.
Fehlt der opake Abschluss, melden SceneEnd/Capture/Wasserhintergrund einen
Fehler statt die Verdeckung nachtraeglich ueber Transparenz zu berechnen.
Wiederholter opaker Abschluss ist idempotent. Nach ihm sind weitere
deckende normale Draws verboten, transparente und additive Draws bleiben
zulaessig. Bei ausgeschalteter AO ist der Aufruf im HDR-Pfad wirkungslos.
View und Perspektive muessen fuer die deckende Depthbasis konstant bleiben;
Modeltransformationen duerfen wechseln.

AO reduziert ausschliesslich den diffusen Ambient-Anteil.
Direktes Licht, GGX-Reflexe und Emission werden nicht pauschal abgedunkelt.
Bei Nebel wird der Ambient-Anteil mit seiner Transmission gewichtet,
nicht das Streulicht. Ein emissives Material mit zusaetzlichem Ambient
kann deshalb insgesamt dunkler werden. Die Tiefennachbarschaft wird auf
halber Aufloesung ausgewertet und kantenbewusst gefiltert; es gibt keine
zeitliche Historie und keine vollstaendige GI-/GTAO-Zusage.

`raum_ao_debug(w, wahr)` / `space_ao_debug` verlangt einen echten Bool
und eine AO-Konfiguration. Es zeigt den tatsaechlich angewandten Faktor
als direkte Graucodewerte: 0 schwarz, 0.5 etwa 128, 1 weiss.
Hintergrund, Radius 0 und Staerke 0 sind weiss.
Die normale Szenenmessung und Bloom laufen weiter; nur die finale
Bildausgabe wird ersetzt. Exposure und Tonemapping veraendern diese
Datenansicht nicht, das HUD wird danach normal gezeichnet.
AO-off schaltet auch Debug aus. HDR-off setzt
AO und Debug zurueck; spaeteres HDR-on reaktiviert sie nicht automatisch.


## Primitive

| Aufruf | Wirkung |
|---|---|
| `raum_dreieck`, `raum_dreieck_farben`, `raum_wuerfel`, `raum_kugel` | beleuchtete Geometrie (auch innerhalb `chunk_beginne`/`chunk_ende`) |
| `raum_linie(w, x1, y1, z1, x2, y2, z2, farbe)` | unbeleuchtete 3D-Linie (1 px), sofort gezeichnet |
| `raum_punkt(w, x, y, z, groesse_px, farbe)` | unbeleuchteter 3D-Punkt (Sterne, Funken) |
| `zeichne_text_z(w, text, x, y, z, groesse_px, farbe)` | Text im Hybrid-Fenster (DejaVu, Umlaute, `\n`) |

## Sprache und Standardbibliothek (fuer Weltgeneratoren)

- `rauschen_2d(x, y[, seed])`, `rauschen_3d(x, y, z[, seed])`, `rauschen_fbm(x, y, oktaven[, seed])`: deterministisches Rauschen im Bereich -1..1.
- `asin`/`arkussinus`, `acos`/`arkuskosinus`, `atan2`/`arkustangens2`.
- Listen: `l.leeren()`, `l.entfernen(i)` (gibt das Element zurueck), `l.einfügen(i, wert)`.
- `global name` in einer Funktion: `setze name auf ...` schreibt dann die Top-Level-Variable.
- `importiere "../lib/welt"`: Pfad relativ zur importierenden Datei; ein fehlendes Modul ist ein Fehler, der die durchsuchten Orte nennt.
- `%` rechnet wie in C (`-7 % 3 == -1`).

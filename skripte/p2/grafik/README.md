# P2-Grafik: CPU-Referenz und API-Entwurf

Dies sind eigenständige C11-Vergleichsfunktionen für P2-T25/T26, keine Moo-
Produkt-API und kein GPU-Renderer. `P2Mesh` kopiert Position/Farbe/Normale und
u32-Indizes beim Anlegen; die CPU-Transformation expandiert Positionen in
Indexreihenfolge, zeichnet aber keine Pixel. So lassen sich Aufbau/Kopie und
CPU-Transform getrennt messen; GPU-Upload, Drawcalls, Shader, Bildtreue und
Lebensdauer eines echten GL-Kontexts sind damit nicht gemessen.

`p2_mat4_mul` entspricht der Rechenreihenfolge von
`compiler/runtime/moo_3d_math.c:15`: f32, Spaltenlayout und Matrix mal
Spaltenvektor. Das ist der C-Adaptervertrag, nicht ein universelles mat4-Layout.
Beide Matrixfunktionen erlauben Ausgabealias mit ihren Eingaben.
Für Vergleiche `-O2 -ffp-contract=off`, kein Fast-Math; Flags und Hardware
dokumentieren. Testergebnisse sind keine Performanceaussage.

## CPU-Test (Linux, keine Fenster/GPU)

```sh
mise run -C skripte/p2/grafik test-cpu
```

Der Test linkt die vorhandene C-Matrixfunktion nur als Differenzreferenz.
ASan/UBSan zusätzlich mit `-O1 -g -fsanitize=address,undefined
-fno-omit-frame-pointer` und `ASAN_OPTIONS=detect_leaks=1` ausführen.

## Entwurf für spätere Moo-API (keine implementierte Syntax)

```text
@native funktion mesh_erstellen(fenster: Fenster, vertices: puffer[Vertex],
                               indices: puffer[u32]) -> Ergebnis[Mesh]
@native funktion mesh_zeichnen(mesh: Mesh, transform: mat4) -> Ergebnis[void]
@native funktion mesh_freigeben(mesh: Mesh) -> void
```

Alternativ ein explizit spezifiziertes `puffer[f32]` mit neun Elementen pro
Vertex (Position, Farbe, Normale), exakten Längen und festgelegter Reihenfolge;
keine implizite Gleichsetzung mit beliebigem `puffer[Vertex]`/GPU-Layout.
`Ergebnis`, Handle- und Fehlersyntax sind Platzhalter. Leere Eingaben,
Vielfaches von drei, gültige Indizes sowie Größe×Stride sind zu prüfen.
Zunächst Kopie beim Aufruf: Puffer bleibt Eigentum des Aufrufers, Mesh gehört
einem lebenden Backendkontext. Fremd-/GPU-Nutzung eines geborgten Puffers nur
mit explizitem Pin bis Nutzungsende und Synchronisation; Resize bei aktivem
Pin verboten. Delete/Context-Close/Hybrid und Fehler-/Double-Free-Vertrag
müssen im echten Backend definiert und getestet werden. GL33 zuerst, GL21 und
Vulkan gemäß Capability-Matrix; altes Chunk-/Immediate-API bleibt bestehen.
GL33-Chunk nutzt derzeit `glDrawArrays`; Indizes sind in dieser Referenz ein
neuer Vertrag, kein Beleg für vorhandenes `glDrawElements`.

Matrix- und Mesh-Funktionen sind kleine eigenständige C-Dateien für den
Benchmark-Agenten; Benchmarks müssen variable Eingaben und beobachtbare
Ergebnisse nutzen, Kern, Aufbau/Kopie, Grenzübergänge und GPU-Transfer separat
führen. Die Referenz behandelt nur CPU-Aufbau und CPU-Transformation.

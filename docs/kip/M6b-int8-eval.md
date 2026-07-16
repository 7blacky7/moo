# KI-M6b EVAL: int8-Inferenzquantisierung

Stand: 2026-07-11 · Entscheidung: **NO-GO für Produktivcode**

## Vorab festgelegte Gates

- MNIST: höchstens −0,5 Prozentpunkte Accuracy.
- Mini-LM: höchstens +2 % Perplexity.
- Gewichtsspeicher: mindestens 65 % Ersparnis.
- Performance/VRAM: positiver Nettoeffekt auf der Zielkarte RTX 2070 8 GB.

Safetensors-Import quantisierter Fremdmodelle bleibt ausdrücklich außerhalb
des Scopes. f32 bleibt Hauptpfad; bf16-Training bleibt unverändert.

## Methode

Die reproduzierbaren EVAL-Programme `eval_m6b_mnist.moos` und
`eval_m6b_mini_lm.moos` trainieren die bestehenden Referenzmodelle unverändert.
Danach simulieren sie ausschließlich für 2D-dicht/matmul-Gewichte eine
symmetrische per-output-channel-Quantisierung:

    scale[j] = max_i(abs(w[i,j])) / 127
    q[i,j] = clamp(round(w[i,j] / scale[j]), -127, 127)
    w_eval[i,j] = q[i,j] * scale[j]

Das sofortige Dequantisieren nach f32 isoliert den Genauigkeitseffekt. Es ist
kein Runtime-Builtin, keine .mook-Erweiterung und kein int8-Kernel.

## Ergebnisse

| Gate | f32 | int8 per-channel simuliert | Delta | Ergebnis |
|---|---:|---:|---:|---|
| MNIST Test-Accuracy | 97,3 % | 97,3 % | 0,0 pp | PASS |
| Mini-LM Eval-Loss | 1,84094 | 1,84132 | +0,000386 | PASS |
| Mini-LM Perplexity | e^1,84094 | e^1,84132 | +0,0386 % | PASS |
| MNIST Dense-Gewichte | 406 528 B | 102 184 B | −74,9 % | PASS |
| Mini-LM Matmul-Gewichte | 411 136 B | 107 672 B | −73,8 % | PASS |

Mini-LM-Training war deterministisch sinkend: Checkpoints 2,62077 → 2,33601
→ 2,10382 → 1,95188. Der Eval-Split besteht aus zwei festen 64-Token-Blöcken.

## Hardwareentscheid

Die RTX 2070 8 GB steckt laut `P0-zielprofil.md` im Unraid-Server und ist in
diesem Projekt-Workspace nicht als Compute-Gerät verfügbar. Lokal vorhanden
ist nur die RTX 4070 Ti 12 GB.

Die zwei im Task erlaubten Implementierungsrichtungen wurden so bewertet:

1. **Dequant-on-load: verworfen.** Das Checkpointfile würde kleiner, aber nach
   dem Laden liegen wieder f32-Gewichte im VRAM. Das verfehlt gerade das
   8-GB-Ziel und erfüllt den vorgeschriebenen VRAM-Nutzen nicht.
2. **int8-Matmul-Kernel: einziger sinnvoller Kandidat.** Er kann die gemessene
   ~74-%-Gewichtsersparnis im VRAM erhalten, muss aber inklusive Scale-Reads,
   Akkumulation und Dequant-Overhead auf der RTX 2070 gemessen werden.

Ohne diese Zielhardwaremessung wäre ein GO eine Vermutung. Deshalb entstehen
jetzt bewusst weder `quantisiere(netz, "int8")`, noch .mook-vNext, noch ein
Shader. Das ist die geforderte P0-Disziplin.

## Reproduzieren

    mise exec -- compiler/target/release/moo-compiler run beispiele/eval_m6b_mnist.moos
    mise exec -- compiler/target/release/moo-compiler run beispiele/eval_m6b_mini_lm.moos

## Wiederaufnahme-Gate

Sobald die RTX 2070 als isolierter Compute-Workspace verfügbar ist:

- EVAL-only int8-Matmul-Mikrokernel gegen f32 auf repräsentativen Formen
  (MNIST 784×128, FFN 64×256 und mindestens M-A-nahe 256×1024) messen.
- GO nur bei mindestens 65 % residenter Gewichtsersparnis **und** keinem
  End-to-End-Inferenz-Rückschritt; Accuracy/Perplexity-Gates erneut ausführen.
- Erst danach genau einen Produktpfad bauen: int8-Kernel, rückwärtskompatibles
  .mook-Versionfeld, ein Kinderleicht-Builtin, Roundtrip/ASan/Doku.

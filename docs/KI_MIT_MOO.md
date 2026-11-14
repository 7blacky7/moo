# KI mit moo – Einstieg

Diese Anleitung richtet sich an alle, die das Repository frisch geklont haben und
(a) moo bauen und ausführen sowie (b) mit dem eingebauten KI-Stack arbeiten wollen:
Tensoren, Autograd, neuronale Netze bis zum Transformer, Training auf CPU oder
Vulkan-GPU, Tokenizer (Zeichen, BPE, deutsche Silben) und Trainingsmethoden wie
SFT, Distillation, DPO und Evaluation.

Alle Befehle werden aus dem Wurzelverzeichnis des Repositorys ausgeführt.

---

## 1. Voraussetzungen

Siehe [DEPENDENCIES.md](../DEPENDENCIES.md). Kurzfassung:

- **Rust** (cargo) – baut den Compiler.
- **LLVM 18** – der Compiler nutzt `inkwell` mit Feature `llvm18-1`.
- **C-Compiler** (gcc/clang) – die Runtime (`compiler/runtime/*.c`) wird über `build.rs` mitgebaut.
- System-Bibliotheken für Grafik/UI/DB (u. a. SDL2, GLFW, Mesa/OpenGL, SQLite, GTK3).
- **Vulkan** (Loader + Treiber) – optional für GPU-Training. Das Cargo-Feature `vulkan`
  ist standardmäßig aktiv; ohne verfügbare GPU rechnet der KI-Stack auf der CPU.

## 2. Bauen

```sh
cd compiler
cargo build --release
cd ..
```

Das Ergebnis ist `compiler/target/release/moo-compiler`. Überblick über die Befehle:

```sh
./compiler/target/release/moo-compiler --help
# Befehle u. a.: compile, run, repl, targets, paket, ast, bindgen
```

## 3. Ausführen

```sh
# kompilieren und sofort ausführen
./compiler/target/release/moo-compiler run beispiele/fibonacci.moos

# native Binary erzeugen
./compiler/target/release/moo-compiler compile beispiele/ki_xor.moos -o ki_xor
./ki_xor
```

Kurzer Check, ob alles läuft – eine Datei `hallo.moos`:

```moo
zeige "Hallo Welt"
```

```sh
./compiler/target/release/moo-compiler run hallo.moos
```

Zur Sprache selbst: [docs/lernen.md](lernen.md).

---

## 4. KI-Schnellstart

### 4.1 Tensor, Autograd und ein kleines Netz (XOR) – `beispiele/ki_xor.moos`

Der vollständige Low-Level-Weg: Tensor anlegen, Schichten bauen, Verlust berechnen,
`rueckwaerts()` (Autograd), Optimiererschritt.

```moo
setze x auf tensor_aus_liste([[0,0],[0,1],[1,0],[1,1]])
setze ziel auf tensor_aus_liste([[0],[1],[1],[0]])
setze netz auf [schicht_dicht(2, 8, "tanh", 7), schicht_dicht(8, 1, "sigmoid", 8)]
setze opt auf optimierer_adam(parameter(netz), 0.05)
setze i auf 0
solange i < 400:
    setze fehler auf mse(vorwaerts(netz, x), ziel)
    fehler.rueckwaerts()
    opt.schritt()
    setze i auf i + 1
autograd_aus()
zeige "Vorhersage (Ziel 0,1,1,0): " + text(vorwaerts(netz, x).zu_liste())
```

```sh
./compiler/target/release/moo-compiler run beispiele/ki_xor.moos
```

Ausgabe (geprüft):

```
Verlust nach 400 Iterationen: [2.20211e-05]
Vorhersage (Ziel 0,1,1,0): [0.00425452, 0.995528, 0.995059, 0.0050562]
```

### 4.2 Die „Kinderleicht“-API: trainieren, speichern, laden – `beispiele/ki_kinderleicht.moos`

```moo
setze netz auf ki_netz([schicht_dicht(2, 8, "tanh", 7), schicht_dicht(8, 1, "sigmoid", 8)])
setze verlauf auf netz.trainiere(daten, ziele, {"epochen": 400, "rate": 0.05, "batch": 4, "ausgabe": 0})
zeige "Genauigkeit: " + text(netz.genauigkeit(daten, ziele))
netz.speichern("/tmp/xor_netz.mook")
setze kopie auf ki_laden("/tmp/xor_netz.mook")
```

```sh
./compiler/target/release/moo-compiler run beispiele/ki_kinderleicht.moos
```

Ausgabe (geprüft): `Genauigkeit: 1`, das geladene Netz liefert exakt dieselbe Vorhersage.
Optionen werden als Dict übergeben. Modelle werden im `.mook`-Format gespeichert.

### 4.3 Tokenizer: Zeichen und deutsche Silben – `beispiele/p4_silben_demo.moos`

Trainiert einen Zeichen-Tokenizer und einen Silben-Tokenizer (auf Basis der
Silbentrennmuster in `compiler/runtime/data/silben/`), speichert, lädt und
prüft den Roundtrip. Kernzeilen:

```moo
setze muster auf datei_lesen("compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat")
setze ct auf zeichen_fixed_trainiere(korpus, 320)
setze st auf silben_trainiere(korpus, 320, muster)
silben_speichern(st, s_pfad)
setze sl auf silben_laden(s_pfad)
silben_dekodiere(sl, silben_kodiere(sl, probe))
```

Das Beispiel braucht ein **leeres, existierendes** Arbeitsverzeichnis:

```sh
mkdir -p /tmp/p4demo   # muss leer sein
MOO_P4_DEMO_DIR=/tmp/p4demo ./compiler/target/release/moo-compiler run beispiele/p4_silben_demo.moos
# Ausgabe (geprüft): P4 DEMO OK
```

Ein BPE-Tokenizer entsteht analog mit `tokenizer_trainiere(korpus, vokabgroesse)`,
`tokenizer_kodiere` / `tokenizer_dekodiere` / `tokenizer_speichern` / `tokenizer_laden`
(siehe `beispiele/p4_silben/train_tokenizers.moos`, `beispiele/tests/p4_tokenizer_api.moos`).
Für einfache Zeichen-Sprachmodelle gibt es `text_tokenizer(text)`, das ein Dict mit
`ids`, `vokab` und `id_zu_zeichen` liefert.

### 4.4 Kleines Sprachmodell (Transformer) – `beispiele/ki_sprachmodell.moos`

Ein Mini-Transformer (2 Blöcke, dim 64, ca. 120k Parameter) lernt Zeichen für
Zeichen auf Grimm-Märchen. Ein Lern-Demonstrator, kein Chatbot. Aufbau:

```moo
setze tok auf text_tokenizer(korpus)
setze emb  auf schicht_embedding(vokab, dim, 1)
setze pos  auf schicht_position(block, dim, "gelernt", 2)
setze att1 auf schicht_attention(dim, koepfe, 3)
# ...
funktion modell(ids_block):
    setze x auf vorwaerts([emb], ids_block)
    setze x auf vorwaerts([pos], x)
    setze x auf x + vorwaerts([ln1a, att1], x)     # Residual über Autograd
    # ...
setze verlust auf kreuzentropie(modell(ids.zeilen(p, p + block)), ids.zeilen(p + 1, p + block + 1))
verlust.rueckwaerts()
gradienten_kappen(params, 1.0)
opt.schritt()
```

Generiert wird unter `autograd_aus()` mit Greedy- oder Temperatur-Sampling.

```sh
./skripte/maerchen_download.sh          # Korpus nach beispiele/daten/maerchen.txt laden
./compiler/target/release/moo-compiler compile beispiele/ki_sprachmodell.moos -o ki_lm
./ki_lm
```

> Nicht im Rahmen dieser Anleitung verifiziert (Laufzeit mehrere Minuten, Korpus-Download nötig).

### 4.5 GPU (Vulkan)

Mit dem Standard-Build (Feature `vulkan`) laufen große Tensor-Operationen automatisch auf
der GPU; kleine bleiben auf der CPU (Schwellen laut `compiler/runtime/moo_ki_gpu.c`:
MatMul ab M·K·N ≥ 2^24, elementweise ab 2^20 Elementen). Steuerung über Umgebungsvariablen:

| Variable | Wirkung |
|---|---|
| `MOO_KI_GPU=0` | GPU komplett abschalten |
| `MOO_KI_GPU_ERZWINGEN=1` | Schwellen ignorieren, auch kleine Tensoren auf die GPU |
| `MOO_KI_GPU_STRIKT` | strenger Modus, CPU-Ausführung wird gemeldet |

`gpu_statistik()` / `gpu_statistik_reset()` zeigen, was tatsächlich auf der GPU lief.
Ein komplettes Beispiel mit wiederaufnehmbaren Checkpoints ist
`beispiele/ki_sprachmodell_gpu.moos` (dort den Checkpoint-Pfad an das eigene System anpassen).

```sh
MOO_KI_GPU_ERZWINGEN=1 ./compiler/target/release/moo-compiler run beispiele/ki_xor.moos
```

Das Programm läuft so durch (geprüft); ob tatsächlich eine GPU benutzt wurde, zeigt
nur `gpu_statistik()`.

---

## 5. Fortgeschritten: Trainingsmethoden (SFT, Distillation, DPO, Evaluation)

Unter `beispiele/p5/` liegt eine einheitliche Trainings-API, in moo selbst geschrieben.
`trainiere_methode(forward, daten, konfig)` wählt über `konfig["methode"]`:

| methode | Direktaufruf | Doku |
|---|---|---|
| `sft` | `trainiere_sft` | [README_sft.md](../beispiele/p5/README_sft.md) |
| `korrekturen` | `trainiere_korrekturen` | [README_sft.md](../beispiele/p5/README_sft.md) |
| `distillation` | `trainiere_distillation` | [README_distill_dpo.md](../beispiele/p5/README_distill_dpo.md) |
| `praeferenzen` (DPO) | `trainiere_praeferenzen` | [README_distill_dpo.md](../beispiele/p5/README_distill_dpo.md) |

Evaluation und Checkpoint-Auswahl (`evaluiere_modell`, `checkpoint_waehlen`):
[README_eval.md](../beispiele/p5/README_eval.md). Übersicht: [README_train_api.md](../beispiele/p5/README_train_api.md).
Tests über mise, z. B. `mise run test-p5-train-api` (alle Tasks: `mise tasks`).

---

## 6. Die wichtigsten KI-Funktionen

| Funktion | Zweck |
|---|---|
| `tensor_aus_liste`, `tensor_nullen`, `tensor_einsen`, `tensor_zufall` | Tensoren erzeugen |
| `.zu_liste()`, `.zeilen(a, b)`, `.groesse()`, `.softmax()` | Tensor-Methoden |
| `autograd_an`, `autograd_aus`, `autograd_reset`, `.rueckwaerts()` | Autograd steuern / Gradienten berechnen |
| `schicht_dicht`, `schicht_faltung`, `schicht_pooling`, `schicht_flach`, `schicht_dropout` | Grundschichten |
| `schicht_embedding`, `schicht_position`, `schicht_attention`, `schicht_layernorm`, `schicht_rmsnorm`, `schicht_ffn_gated`, `schicht_moe`, `schicht_attnres` | Transformer-Bausteine |
| `vorwaerts(schichten, x)`, `parameter(schichten)` | Forward-Pass, trainierbare Parameter |
| `mse`, `kreuzentropie` | Verlustfunktionen |
| `optimierer_sgd`, `optimierer_adam`, `optimierer_adamw`, `.schritt()`, `gradienten_kappen` | Optimierung |
| `ki_netz`, `.trainiere`, `.vorhersage`, `.genauigkeit`, `.speichern`, `ki_laden` | Kinderleicht-API |
| `checkpoint_speichern`, `checkpoint_laden`, `checkpoint_rotieren`, `checkpoint_waehlen` | Checkpoints (`.mook`) |
| `text_tokenizer` | einfacher Zeichen-Tokenizer |
| `tokenizer_trainiere`, `tokenizer_kodiere`, `tokenizer_kodiere_stapel`, `tokenizer_dekodiere`, `tokenizer_speichern`, `tokenizer_laden`, `tokenizer_info`, `tokenizer_hash` | BPE-Tokenizer |
| `silben_trainiere`, `silben_kodiere`, `silben_dekodiere`, `silben_speichern`, `silben_laden`, `silben_info` | deutscher Silben-Tokenizer |
| `zeichen_fixed_trainiere`, `zeichen_fixed_kodiere`, `zeichen_fixed_dekodiere`, `zeichen_fixed_speichern`, `zeichen_fixed_laden` | fester Zeichen-Tokenizer |
| `gpu_statistik`, `gpu_statistik_reset` | GPU-Nutzung prüfen |

Viele Funktionen haben zusätzlich englische Aliasse (z. B. `tensor_from_list`,
`autograd_off`, `tokenizer_encode`, `checkpoint_save`).

## 7. Weitere Beispiele und Dokumente

- `beispiele/ki_*.moos` – u. a. `ki_mnist.moos`, `ki_mnist_conv.moos`, `ki_embeddings.moos`,
  `ki_moe.moos`, `ki_grpo.moos`, `ki_mtp.moos`, `ki_multimodal*.moos`,
  `ki_safetensors_import.moos`, `ki_verteilt.moos`.
- `beispiele/p4_silben/` – Silben-Sprachmodell-Pipeline (Tokenizer, Training, Eval).
- `beispiele/p5/` – Trainings-API, SFT, Distillation, DPO, Evaluation.
- [docs/kip/](kip/) – Design- und Vertragsdokumente zum KI-Stack (Dtypes, GPU-Residenz,
  verteiltes Training, Serving, Post-Training).

## 8. Wichtig: Daten und Modelle sind nicht im Repo

Trainingskorpora und Modell-Checkpoints werden **nicht** mitgeliefert. Für eigene
Experimente braucht man eigene Texte (UTF-8). Für das Märchen-Beispiel lädt
`skripte/maerchen_download.sh` einen gemeinfreien Korpus herunter. Einige Beispiele
unter `beispiele/p4_silben/` und `beispiele/p5/` erwarten Pfade bzw. Umgebungsvariablen
(z. B. `P4_TRAIN_CORPUS`) zu eigenen Daten.

# Einheitliche Trainings-API

`gpu_statistik()` enthält zusätzlich `cpu_nur_ops` und `cpu_nur_broadcast_add/sub/mul/div`: Sie zählen elementweise Broadcasts ohne GPU-Kernel, wenn GPU-Nutzung explizit angefordert wurde oder die GPU bereits initialisiert ist; `MOO_KI_GPU=0` deaktiviert die Zählung. Die Zähler belegen für sich keine verfügbare GPU. `cpu_fallbacks` behält seine bisherige Bedeutung (fehlgeschlagene GPU-Operation). Unter STRIKT wird die CPU-Ausführung einmal pro Prozess auf stderr gemeldet; sie bleibt ausführbar. `gpu_statistik_reset()` setzt beide Zählerarten zurück. `mise run test-p5-cpu-nur` prüft Zähler und Warnung.

`trainiere_methode(forward, daten, konfig)` wählt anhand von `konfig["methode"]` die Trainingsmethode. Der Callback erhält Token-IDs in voller Positionslänge und liefert Logits [Sequenzlänge, Vokabular]. Die Architektur und die tatsächlich verwendeten Parameter liegen beim Aufrufer.

| methode | Direktaufruf | Daten und zusätzliche Konfiguration |
| --- | --- | --- |
| `sft` | `trainiere_sft` | Maskierte Beispiele; siehe [SFT](README_sft.md). |
| `korrekturen` | `trainiere_korrekturen` | Akzeptierte Korrekturfenster und `encoder`; siehe [SFT](README_sft.md). |
| `distillation` | `trainiere_distillation` | Lehrer-Logits, gleiche Tokenizer-ID, `temperatur`, `alpha`; siehe [Distillation](README_distill_dpo.md). |
| `praeferenzen` | `trainiere_praeferenzen` | Gewähltes und verworfenes Beispiel, Referenz-Callback und Parameter, `beta`; siehe [Präferenzen](README_distill_dpo.md). |

Gemeinsame Konfiguration: `tokenizer_id`, `sequenzlaenge`, `vokab`, `params`, `schritte`; für einen neuen Optimierer `optimierer` und `rate`, bei Resume `opt`. Alle Methoden liefern `opt`, `verluste`, `schritte`. Die methodenspezifischen Verträge und Grenzen stehen in den verlinkten Beschreibungen; unbekannte Methoden werden abgelehnt.


Optional: `aus train_orchestrierung importiere trainiere_mit_val` ruft `trainiere_mit_val(forward, daten, konfig, zustand, fabrik, eval_konfig)` auf. Ohne `val` wird unverändert an `trainiere_methode` delegiert. Mit `val` enthält `konfig` zusätzlich `val: {daten: {text, split: "val"}, intervall_schritte}`, `checkpoint: {ordner, jeweils_bei_val: wahr}`, `auswahl: "beste_val"` und optional `fruehstopp: {geduld_evals, min_verbesserung}`. `zustand` ist der zu speichernde Checkpointzustand mit Modell und Metadaten; `fabrik` und `eval_konfig` entsprechen der Evaluations-API. Das Verzeichnis muss vorhanden sein; vorhandene Zieldateien werden abgelehnt. Das Ergebnis enthält `val_berichte`, `bester_checkpoint`, `fruehstopp` und den letzten Optimiererzustand. Frühstopp vergleicht NLL/Byte auf Val; die Auswahl nutzt `checkpoint_waehlen`. Der beste Checkpoint wird als Bericht/Pfad zurückgegeben und nicht automatisch in das laufende Modell geladen. Der Aufrufer muss sicherstellen, dass Forward-Callback, `params` und Checkpointzustand dasselbe Modell verwenden. Die Prüfung auf vorhandene Ausgabedateien ist keine atomare Sperre; das Verzeichnis muss einem Lauf exklusiv gehören. Die Schrittzähler eines fortgesetzten Zustands bleiben erhalten. Beispiel und Regression: `train_orchestrierung_test.moos`, `mise run test-p5-train-orchestrierung`.

`evaluiere_modell` und `checkpoint_waehlen` sind separat dokumentiert: [Evaluation](README_eval.md). `mise run test-p5-train-api` prüft den Dispatcher an echten Trainingsschritten und Negativfällen.

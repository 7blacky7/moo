# KIP-X3 — Post-Training-Design: Verträge für T3/B4a/E1 (SFT, DPO, GRPO)

Stand: 2026-07-10. Task c7da18d2. Status: Entwurf, GPT-Review pending.
NUR Design — kein Code. Liefert bindende Verträge an KIP-T3 (Chat-Template),
KIP-B4a (Loss-Maskierung) und KIP-E1 (Shard-Metadaten).

## 1. VERTRAG AN KIP-T3: Chat-Template
- Rollen: system, user, assistant, tool (Aufruf) + tool_result. Jede Rolle =
  Start-Token `<|rolle|>` + Inhalt + gemeinsames `<|ende|>`-Token. Ein globales
  EOS beendet den Dialog. BOS optional pro Sequenz.
- Render-API: `template_rendern(dialogliste)` → { ids, loss_maske, stop_ids }.
  Die LOSS-MASKE entsteht beim Rendern (einzige Stelle mit Rollenwissen):
  1 auf Assistant-INHALT + dessen `<|ende|>`, 0 auf allem anderen (System,
  User, Rollen-Start-Tokens, Tool-Results). Trainings-Code kennt keine Rollen.
- Injection-Schutz (Security-Vertrag aus 7b5eb6e2 Pkt. 7d) gilt: Inhalte werden
  byte-tokenisiert, Special-IDs nur out-of-band durch die Render-API.
- Tool-Aufrufe: strukturierte Ausgabe als getaggter Block IM Assistant-Inhalt
  (`<|werkzeug|>`name + JSON`<|/werkzeug|>` als reservierte Tokens). Parsing =
  Userland; Training behandelt den Block als normale Assistant-Tokens.
  INJECTION-Pflicht (GPT 49573fb4): Werkzeug-Tokens werden AUSSCHLIESSLICH
  von der Render-API out-of-band eingefügt — rohe User- UND Tool-Result-
  Texte können reservierte Werkzeug-Tokens NIE erzeugen (Negativ-Tests
  analog Rollen-Token-Injection).

## 2. VERTRAG AN KIP-B4a: Loss-Maskierung
- B4a erhält pro gepacktem Block DREI Masken, alle aus denselben Doc-Grenzen
  abgeleitet: (a) blockdiagonale Attention-Maske, (b) Positions-Reset-Liste,
  (c) LOSS-Maske. Für Pretraining ist (c) trivial (alles 1 außer Grenzen);
  für SFT-Daten kommt (c) FERTIG aus dem Shard (siehe 3.) bzw. aus
  `template_rendern` — B4a berechnet SFT-Loss-Masken NIE selbst.
- CE-Loss-API braucht eine maskierte Variante: `kreuzentropie(logits, ziele,
  maske)` = Summe nur über maske==1, normiert über Anzahl maskierter Tokens
  (nicht Batch-Größe). Op-Komposition prüfen (mul mit grad-loser Maske +
  sum/anzahl) — voraussichtlich OHNE neuen Registry-Op darstellbar.

## 3. VERTRAG AN KIP-E1: Shard-Metadaten
- Shard-Header-Felder (Pflicht): format_version, tokenizer_version (T2-Artefakt-
  Hash), doc_offsets (für Packing-Grenzen), datenart = "pretrain" | "sft".
- SFT-Shards speichern pro Dokument ZWEI parallele Spuren: token_ids (u32) und
  loss_maske (packbar als Bitfeld). Pretrain-Shards lassen die Maskenspur weg
  (Flag im Header). E1-Loader liefert beides an den Trainings-Loop.
- Optionale Felder (reserviert, nicht Pflicht in E1): quelle, lizenz_tag —
  Vorgriff auf die Datenfabrik-Zerlegung, kostet im Header nichts.

## 4. Trainings-Stufen (Umsetzungsphase, je eigenes Gate)
1. SFT: maskierte CE auf Chat-Template-Daten. GATE: deterministischer Toy-Dialog-
   Korpus, Loss sinkt nur auf Assistant-Tokens (Gegenprobe: User-Token-Loss
   unverändert), Golden-Maske-Tests.
2. Preference (DPO zuerst): loss = -log(sigmoid(beta*((lp_c^pol - lp_r^pol) -
   (lp_c^ref - lp_r^ref)))). Logprob-Summen je Sequenz = maskierte-CE-Bausteine;
   Referenzmodell = eingefrorene Kopie (autograd_aus + zweites Netz-Handle).
   Voraussichtlich reine Op-Komposition (logsoftmax/mul/sum/sigmoid vorhanden).
   NUMERIK (Pflicht, GPT 49573fb4): log(sigmoid(x)) als -softplus(-x)
   formulieren (max-shift-stabil) — darf bei großen negativen Margins nie
   -inf liefern. MASKEN-Vertrag: DPO nutzt ausschließlich die Response-/
   Assistant-Maske bei IDENTISCHEM Prompt für chosen und rejected.
   DPO-GATES: beta=0 → loss==log(2); policy==reference → log(2);
   chosen/rejected-Tausch dreht das Vorzeichen der Margin korrekt;
   Referenzmodell erhält NIE Gradienten (Gegenprobe); Extremwert-Margins
   bleiben endlich.
   Falls doch neuer Op nötig: STOPP + B2-Vertrag. IPO/KTO = Vergleichsoptionen;
   DPO ist die erste Preference-BASELINE, weil breit dokumentiert und
   mechanisch testbar (paarweiser Pfad, eingefrorene Referenz, prüfbare
   Formel) — nicht weil "stabilster Standard".
3. GRPO produktiv: ki_grpo-Toy-Mechanik (Gruppen-Baseline, advantage-gewichtete
   CE) auf Sequenz-Ebene mit Reward-API; verifizierbare Envs zuerst (Rechnen,
   Format-Compliance). GATE: Toy-Gate-Muster auf M-A-Modell reproduziert.
4. Distillation: Fremd-Logits sind ohne Fremd-Runtime unrealistisch → Pfad =
   Sequenz-Level-Distillation (Lehrer-TEXTE als SFT-Daten). Ehrlich als solche
   benannt; Logit-Distillation nur falls später ein Lehrer in moo läuft (D3-
   Import). Kein Task jetzt.

## 5. Reihenfolge-Konsequenz
T3 kann nach GPT-GO dieses Docs + X2 sofort starten; B4a nach B2; E1 nach T2.
SFT (Stufe 1) ist der erste Post-Training-Implementierungstask NACH M-A-Gate —
wird erst dann als Task angelegt (keine Vorrats-Tasks).

## 6. Exit-Gate
GPT-Review: GO nach Addendum (49573fb4) — Addendum eingearbeitet (Numerik,
Masken-Vertrag, DPO-Gates, Werkzeug-Token-Injection). Vertrags-Abnahme durch
T3-, B4a- und E1-Implementierer.

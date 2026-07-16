# ============================================================
# Fremd-safetensors-Import (Plan-014 F1-Gate).
# Vorher:  python3 skripte/make_ref_safetensors.py
# Die Referenzdatei wurde mit purer Python-Stdlib nach der
# safetensors-SPEZIFIKATION geschrieben (nicht mit der Library) —
# dieser Test beweist Format-Kompatibilitaet in beide Richtungen.
# Erwartet: Forward-Ergebnis exakt [2.5, 5] (Referenz im Script).
# ============================================================
setze gewichte auf safetensors_laden("/tmp/moo_f1_ref.safetensors")
zeige "Geladene Tensoren: w=" + text(gewichte["w"].form()) + " b=" + text(gewichte["b"].form())

setze x auf tensor_aus_liste([[1, 2]])
setze y auf x.matmul(gewichte["w"]) + gewichte["b"]
zeige "Forward: " + text(y.zu_liste())
zeige "Referenz: [2.5, 5]"

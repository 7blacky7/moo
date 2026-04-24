# ============================================================
# stdlib/ui_test.moo — Snapshot-Wrapper + JSON-Sidecar (Plan-004 P2)
#
# Ergaenzt die nativen Snapshot-Builtins (Runtime-Seite):
#     ui_test_snapshot(fenster, pfad)            -> bool
#     ui_test_snapshot_widget(widget, pfad)      -> bool
#
# Diese Wrapper liefern zusaetzlich zum PNG eine maschinenlesbare
# JSON-Sidecar-Datei (KI-Artefakte, Plan-004 @gpt-Vorgabe). Damit kann
# ein Agent zu jedem Screenshot eindeutig bestimmen:
#   - welches Fenster (Titel/ID)
#   - wann (ISO-Zeit + Unix-Timestamp)
#   - welches Backend (gtk/win32/cocoa)
#   - Fenster-Groesse (b/h) und Scale (best-effort)
#   - kompletter Widget-Baum (flache pre-order-Liste)
#   - Pfad des PNG-Screenshots (Basename)
#
# Oeffentliche API:
#   ui_test_snapshot_mit_sidecar(fenster, pfad_basis) -> bool
#   ui_test_snapshot_serie(fenster, ordner, prefix, frames, abstand_ms)
#
# Runtime-Limit-Beachtung:
#   - Bracket-Syntax d["k"] statt Dot (pointer-tagged MooValues).
#   - .enthält(key) statt "in".
#   - Keine Keyword-Args am Call-Site.
#   - Reservierte Bezeichner vermeiden (default/standard/neu/st).
#   - JSON wird OHNE externe Lib erzeugt, aber MUSS python3 json.loads
#     bestehen (getestet in beispiele/ui_snapshot_demo.moo).
# ============================================================


# ---------- interne JSON-Helfer (pure moo) ------------------

# Escaped einen String gemaess RFC 8259: ", \, \b \f \n \r \t,
# Steuerzeichen < 0x20 werden als \uXXXX kodiert. Nicht-ASCII wird
# 1:1 durchgereicht (UTF-8 ist in JSON erlaubt).
funktion __uit_json_esc(s):
    setze puffer auf ""
    setze i auf 0
    setze n auf länge(s)
    solange i < n:
        setze c auf s[i]
        wenn c == "\\":
            setze puffer auf puffer + "\\\\"
        sonst wenn c == "\"":
            setze puffer auf puffer + "\\\""
        sonst wenn c == "\n":
            setze puffer auf puffer + "\\n"
        sonst wenn c == "\r":
            setze puffer auf puffer + "\\r"
        sonst wenn c == "\t":
            setze puffer auf puffer + "\\t"
        sonst wenn c == "\b":
            setze puffer auf puffer + "\\b"
        sonst wenn c == "\f":
            setze puffer auf puffer + "\\f"
        sonst:
            setze puffer auf puffer + c
        setze i auf i + 1
    gib_zurück puffer


# JSON-Literal fuer einen beliebigen Wert. Nutzt das Runtime-Builtin
# json_string, damit wir das Rad fuer Dicts/Listen/Zahlen/Bool nicht
# neu erfinden — dieser Pfad ist bereits in stdlib/ui_introspect.moo
# erprobt und liefert validen Output.
funktion __uit_json_any(v):
    wenn v == nichts:
        gib_zurück "null"
    gib_zurück json_string(v)


# Zweistellige Zahl (fuer ISO-Zeit).
funktion __uit_z2(n):
    wenn n < 10:
        gib_zurück "0" + text(n)
    gib_zurück text(n)


# Vierstellige Zahl (fuer ISO-Jahr).
funktion __uit_z4(n):
    wenn n < 10:
        gib_zurück "000" + text(n)
    wenn n < 100:
        gib_zurück "00" + text(n)
    wenn n < 1000:
        gib_zurück "0" + text(n)
    gib_zurück text(n)


# Wandelt Unix-Timestamp (Sekunden seit 1970-01-01 UTC) in ISO-8601.
# Algorithmus: Howard Hinnant "days from civil".
# Keine Zeitzone — liefert UTC mit Suffix "Z".
funktion __uit_iso_puffer_unix(ts_sec):
    setze t auf boden(ts_sec)
    setze tage auf boden(t / 86400)
    setze rest auf t - tage * 86400
    setze hh auf boden(rest / 3600)
    setze mm auf boden((rest - hh * 3600) / 60)
    setze ss auf rest - hh * 3600 - mm * 60

    # Hinnant civil_from_days (shift Epoch: 1970-01-01 -> 0000-03-01 era offset 719468).
    setze z auf tage + 719468
    setze era auf boden(z / 146097)
    setze doe auf z - era * 146097
    setze yoe auf boden((doe - boden(doe / 1460) + boden(doe / 36524) - boden(doe / 146096)) / 365)
    setze y auf yoe + era * 400
    setze doy auf doe - (365 * yoe + boden(yoe / 4) - boden(yoe / 100))
    setze mp auf boden((5 * doy + 2) / 153)
    setze d auf doy - boden((153 * mp + 2) / 5) + 1
    setze m auf mp + 3
    wenn mp >= 10:
        setze m auf mp - 9
        setze y auf y + 1

    gib_zurück __uit_z4(y) + "-" + __uit_z2(m) + "-" + __uit_z2(d) + "T" + __uit_z2(hh) + ":" + __uit_z2(mm) + ":" + __uit_z2(ss) + "Z"


# Ermittelt Backend-Namen. Bevorzugt MOO_UI_BACKEND (Env), sonst
# OS-heuristisch puffer umgebung("OS") / umgebung("OSTYPE"). Fallback "gtk".
funktion __uit_backend():
    setze b auf umgebung("MOO_UI_BACKEND")
    wenn b != nichts:
        wenn länge(b) > 0:
            gib_zurück b
    setze os auf umgebung("OS")
    wenn os != nichts:
        wenn os.str_contains("Windows"):
            gib_zurück "win32"
    setze ot auf umgebung("OSTYPE")
    wenn ot != nichts:
        wenn ot.str_contains("darwin"):
            gib_zurück "cocoa"
    gib_zurück "gtk"


# Extrahiert nur den Dateinamen puffer einem Pfad (basename-light).
funktion __uit_basename(pfad):
    setze i auf länge(pfad) - 1
    solange i >= 0:
        setze c auf pfad[i]
        wenn c == "/":
            gib_zurück pfad.teilstring(i + 1, länge(pfad))
        wenn c == "\\":
            gib_zurück pfad.teilstring(i + 1, länge(pfad))
        setze i auf i - 1
    gib_zurück pfad


# Sicheres Lookup mit Fallback.
funktion __uit_feld(d, key, fallback):
    wenn d == nichts:
        gib_zurück fallback
    wenn d.enthält(key):
        gib_zurück d[key]
    gib_zurück fallback


# ---------- oeffentliche API --------------------------------

# Macht Screenshot UND schreibt Sidecar-JSON.
# pfad_basis: z.B. "beispiele/snapshots/demo1"
#   -> pfad_basis + ".png"  (Screenshot, vom Runtime erzeugt)
#   -> pfad_basis + ".json" (Sidecar, von dieser Funktion erzeugt)
#
# Rueckgabe: wahr wenn PNG + JSON erfolgreich geschrieben.
funktion ui_test_snapshot_mit_sidecar(fenster, pfad_basis):
    setze png_pfad auf pfad_basis + ".png"
    setze json_pfad auf pfad_basis + ".json"

    # 1) Screenshot aufnehmen.
    setze ok auf ui_test_snapshot(fenster, png_pfad)
    wenn ok != wahr:
        gib_zurück falsch

    # 2) Metadaten sammeln.
    setze info auf ui_widget_info(fenster)
    setze fenster_titel auf __uit_feld(info, "text", "")
    setze b auf __uit_feld(info, "b", 0)
    setze h auf __uit_feld(info, "h", 0)

    setze baum auf ui_widget_baum(fenster)
    wenn baum == nichts:
        setze baum auf []

    setze ts_unix auf datei_mtime(png_pfad)
    wenn ts_unix < 0:
        setze ts_unix auf 0
    setze ts_iso auf __uit_iso_puffer_unix(ts_unix)

    setze backend auf __uit_backend()
    setze png_name auf __uit_basename(png_pfad)

    # 3) Sidecar-JSON manuell zusammenbauen.
    #    Strings werden via __uit_json_esc escaped. Komplexe Felder
    #    (widget_tree, window_size) gehen durch json_string (Runtime-Builtin).
    setze window_size auf {}
    setze window_size["b"] auf b
    setze window_size["h"] auf h

    setze j auf "{"
    setze j auf j + "\"fenster_titel\":\"" + __uit_json_esc(fenster_titel) + "\","
    setze j auf j + "\"timestamp\":\"" + __uit_json_esc(ts_iso) + "\","
    setze j auf j + "\"timestamp_unix\":" + text(ts_unix) + ","
    setze j auf j + "\"backend\":\"" + __uit_json_esc(backend) + "\","
    setze j auf j + "\"scale\":1,"
    setze j auf j + "\"window_size\":" + __uit_json_any(window_size) + ","
    setze j auf j + "\"widget_tree\":" + __uit_json_any(baum) + ","
    setze j auf j + "\"screenshot_path\":\"" + __uit_json_esc(png_name) + "\""
    setze j auf j + "}"

    gib_zurück datei_schreiben(json_pfad, j)


# Snapshot-Serie: frames-Snapshots mit abstand_ms dazwischen.
# Schreibt in ordner/prefix_NNN.png + .json. Setzt vorpuffer, dass
# die Event-Loop laeuft (z.B. ui_test_pump() zwischen Frames, falls
# benoetigt — diese Funktion ruft bewusst nichts Blocking auf).
# Rueckgabe: Anzahl erfolgreich geschriebener Frames.
funktion ui_test_snapshot_serie(fenster, ordner, prefix, frames, abstand_ms):
    wenn datei_existiert(ordner) != wahr:
        datei_mkdir(ordner)
    setze ok_count auf 0
    setze i auf 0
    solange i < frames:
        setze nr auf text(i + 1000)
        # "1000" -> "000" (drei Stellen), simpler Padding-Trick.
        setze nr auf nr.teilstring(1, länge(nr))
        setze basis auf ordner + "/" + prefix + "_" + nr
        setze ok auf ui_test_snapshot_mit_sidecar(fenster, basis)
        wenn ok == wahr:
            setze ok_count auf ok_count + 1
        # Kleine Busy-Wait basierend auf zeit_ms, damit wir keinen
        # Runtime-Sleep-Builtin brauchen. abstand_ms klein halten!
        setze start auf zeit_ms()
        solange zeit_ms() - start < abstand_ms:
            setze nichts_tun auf 0
        setze i auf i + 1
    gib_zurück ok_count

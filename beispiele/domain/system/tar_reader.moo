# ============================================================
# moo TAR-Reader — POSIX ustar Archive Parser in pure moo
#
# Kompilieren: moo-compiler compile tar_reader.moo -o tar_reader
# Starten:     ./tar_reader
#
# Liest eine .tar Datei, parst alle Member-Header (POSIX ustar
# 512-byte Block-Layout), listet sie mit Typ/Groesse/Name und
# extrahiert optional den Inhalt einer bestimmten Datei als
# lesbaren Text.
#
# Anforderung: datei_lesen_bytes() — binary-safe File-Read der
# eine Liste 0..255 zurueckgibt. moo_file_read selbst ist nicht
# binary-safe (strlen-Basiert).
# ============================================================

konstante TAR_DATEI auf "/tmp/moo_tar_test.tar"

# --- Byte-Helfer ---
funktion slice_bytes(bs, off, len):
    setze r auf []
    setze i auf 0
    solange i < len:
        r.hinzufügen(bs[off + i])
        setze i auf i + 1
    gib_zurück r

# ASCII-Bytes bis NUL → String
funktion ascii_string(bs, off, max):
    setze ende auf 0
    solange ende < max und bs[off + ende] != 0:
        setze ende auf ende + 1
    gib_zurück bytes_neu(slice_bytes(bs, off, ende))

# Ist ein Bytes-Slice komplett 0 (Ende-Marker)?
funktion alle_null(bs, off, len):
    setze i auf 0
    solange i < len:
        wenn bs[off + i] != 0:
            gib_zurück falsch
        setze i auf i + 1
    gib_zurück wahr

# TAR-Felder sind oktal-ASCII mit NUL oder Space terminiert.
# Beispiel: "0001750\0"  →  1000 dezimal
funktion oct_parse(bs, off, max):
    setze wert auf 0
    setze i auf 0
    solange i < max:
        setze b auf bs[off + i]
        wenn b >= 48 und b <= 55:  # '0'..'7'
            setze wert auf wert * 8 + (b - 48)
        sonst:
            gib_zurück wert
        setze i auf i + 1
    gib_zurück wert

# --- Header-Struktur (POSIX ustar, 512 bytes) ---
# name[100] mode[8] uid[8] gid[8] size[12] mtime[12] chksum[8] typeflag[1]
# linkname[100] magic[6]="ustar\0" version[2]="00" uname[32] gname[32]
# devmajor[8] devminor[8] prefix[155] padding[12]

funktion typeflag_name(tf):
    wenn tf == 0 oder tf == 48:  # '\0' oder '0'
        gib_zurück "datei"
    wenn tf == 49:   # '1'
        gib_zurück "hardlink"
    wenn tf == 50:   # '2'
        gib_zurück "symlink"
    wenn tf == 53:   # '5'
        gib_zurück "verzeichnis"
    wenn tf == 103:  # 'g' pax global
        gib_zurück "pax-global"
    wenn tf == 120:  # 'x' pax extended
        gib_zurück "pax-extended"
    gib_zurück "sonst(" + text(tf) + ")"

funktion parse_tar(bs):
    setze eintraege auf []
    setze off auf 0
    setze total auf bs.länge()

    solange off + 512 <= total:
        # Leer-Block → Ende
        wenn alle_null(bs, off, 512):
            gib_zurück eintraege

        setze name auf ascii_string(bs, off + 0, 100)
        setze mode auf oct_parse(bs, off + 100, 8)
        setze uid auf oct_parse(bs, off + 108, 8)
        setze gid auf oct_parse(bs, off + 116, 8)
        setze size auf oct_parse(bs, off + 124, 12)
        setze mtime auf oct_parse(bs, off + 136, 12)
        setze typeflag auf bs[off + 156]
        setze linkname auf ascii_string(bs, off + 157, 100)
        setze magic auf ascii_string(bs, off + 257, 6)
        setze uname auf ascii_string(bs, off + 265, 32)
        setze gname auf ascii_string(bs, off + 297, 32)
        setze prefix auf ascii_string(bs, off + 345, 155)

        # Voller Pfad: prefix/name (POSIX erlaubt Aufteilung)
        setze voll_name auf name
        wenn länge(prefix) > 0:
            setze voll_name auf prefix + "/" + name

        setze eintrag auf {}
        eintrag["name"] = voll_name
        eintrag["mode"] = mode
        eintrag["size"] = size
        eintrag["uid"] = uid
        eintrag["gid"] = gid
        eintrag["mtime"] = mtime
        eintrag["uname"] = uname
        eintrag["gname"] = gname
        eintrag["typ"] = typeflag_name(typeflag)
        eintrag["ustar"] = magic
        eintrag["data_offset"] = off + 512
        eintraege.hinzufügen(eintrag)

        # Naechster Header: um size aufgerundet auf 512er-Multiple
        setze data_bloecke auf boden((size + 511) / 512)
        setze off auf off + 512 + data_bloecke * 512

    gib_zurück eintraege

funktion oktal_str(n):
    wenn n == 0:
        gib_zurück "0"
    setze s auf ""
    setze v auf n
    solange v > 0:
        setze s auf text(v % 8) + s
        setze v auf boden(v / 8)
    gib_zurück s

# --- Main ---
zeige "=== moo TAR-Reader ==="
zeige "Lese " + TAR_DATEI

wenn nicht datei_existiert(TAR_DATEI):
    zeige "Datei existiert nicht — bitte /tmp/moo_tar_test.tar erstellen"
sonst:
    setze bs auf datei_lesen_bytes(TAR_DATEI)
    zeige "Bytes: " + text(bs.länge())

    setze liste auf parse_tar(bs)
    zeige "Eintraege: " + text(liste.länge())
    zeige ""
    zeige "  MODE    TYP         GROESSE   USER/GRP         NAME"
    zeige "  ----    ---         -------   --------         ----"
    setze i auf 0
    solange i < liste.länge():
        setze e auf liste[i]
        setze mode_s auf oktal_str(e["mode"])
        setze user_s auf e["uname"] + "/" + e["gname"]
        zeige "  " + mode_s + "\t" + e["typ"] + "\t" + text(e["size"]) + "\t" + user_s + "\t" + e["name"]
        setze i auf i + 1

    zeige ""
    zeige "--- Inhalt des ersten Text-Files (ASCII-Darstellung) ---"
    setze i auf 0
    solange i < liste.länge():
        setze e auf liste[i]
        wenn e["typ"] == "datei" und e["size"] > 0 und e["size"] < 2000:
            setze inhalt auf bytes_neu(slice_bytes(bs, e["data_offset"], e["size"]))
            zeige "== " + e["name"] + " =="
            zeige inhalt
            setze i auf liste.länge()
        sonst:
            setze i auf i + 1

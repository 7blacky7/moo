# ============================================================
# moo Mini-Datenbank — B-Tree Index + WAL + Tabellen-Engine
#
# Features:
#   - B-Tree (min_degree t=3, max 5 keys/node) fuer Indexe
#   - Tabellen mit typisiertem Schema
#   - insert / suche / bereich / loeschen (Tombstone)
#   - WAL (Write-Ahead-Log) als JSON-Lines auf Disk
#   - Recovery: replay des WAL beim DB-Start
#
# Architektur:
#   - BTreeKnoten (Blatt + Interior)
#   - BTree (wurzel + min_degree)
#   - Tabelle (schema + btree-Index ueber primary key)
#   - Datenbank (tabellen-Map + WAL-Writer)
#   - Operation-Vererbung: Op -> InsertOp, DeleteOp (fuer WAL-Serialisierung)
# ============================================================

# ------------------------------------------------------------
# B-Tree Knoten
# ------------------------------------------------------------

konstante T auf 3          # min-degree (2*T-1 = 5 max keys)
konstante MAX_KEYS auf 5   # 2*T - 1

klasse BTreeKnoten:
    funktion erstelle(blatt):
        selbst.blatt = blatt
        selbst.schluessel = []      # sortierte Liste von keys
        selbst.werte = []           # parallele Liste von Werten
        selbst.kinder = []          # bei non-leaf: Liste<BTreeKnoten>

# ------------------------------------------------------------
# B-Tree Operationen (funktional, nicht an Klasse gebunden)
# ------------------------------------------------------------

# Binaere Suche innerhalb eines Knotens: findet ersten index i wo schluessel[i] >= k
funktion knoten_suchindex(knoten, k):
    setze lo auf 0
    setze hi auf länge(knoten.schluessel)
    solange lo < hi:
        setze mid auf boden((lo + hi) / 2)
        wenn knoten.schluessel[mid] < k:
            setze lo auf mid + 1
        sonst:
            setze hi auf mid
    gib_zurück lo

funktion btree_suche(knoten, k):
    setze i auf knoten_suchindex(knoten, k)
    wenn i < länge(knoten.schluessel) und knoten.schluessel[i] == k:
        gib_zurück knoten.werte[i]
    wenn knoten.blatt:
        gib_zurück nichts
    gib_zurück btree_suche(knoten.kinder[i], k)

# Split child y = x.kinder[i] (der VOLL ist) in zwei Knoten
funktion btree_split_child(x, i):
    setze y auf x.kinder[i]
    setze z auf neu BTreeKnoten(y.blatt)
    # z bekommt die oberen (T-1) keys/values
    setze j auf T
    solange j < länge(y.schluessel):
        z.schluessel.hinzufügen(y.schluessel[j])
        z.werte.hinzufügen(y.werte[j])
        setze j auf j + 1
    wenn y.blatt == falsch:
        setze j auf T
        solange j < länge(y.kinder):
            z.kinder.hinzufügen(y.kinder[j])
            setze j auf j + 1
    # Median (y.schluessel[T-1]) hoch nach x[i]
    setze median_k auf y.schluessel[T - 1]
    setze median_v auf y.werte[T - 1]
    # y kuerzen auf erste T-1 keys
    setze neue_k auf []
    setze neue_v auf []
    setze j auf 0
    solange j < T - 1:
        neue_k.hinzufügen(y.schluessel[j])
        neue_v.hinzufügen(y.werte[j])
        setze j auf j + 1
    setze y.schluessel auf neue_k
    setze y.werte auf neue_v
    wenn y.blatt == falsch:
        setze neue_c auf []
        setze j auf 0
        solange j < T:
            neue_c.hinzufügen(y.kinder[j])
            setze j auf j + 1
        setze y.kinder auf neue_c
    # x.schluessel[i] einfuegen, z als neues kind hinter y
    setze neue_xk auf []
    setze neue_xv auf []
    setze neue_xc auf []
    setze j auf 0
    solange j < i:
        neue_xk.hinzufügen(x.schluessel[j])
        neue_xv.hinzufügen(x.werte[j])
        setze j auf j + 1
    neue_xk.hinzufügen(median_k)
    neue_xv.hinzufügen(median_v)
    setze j auf i
    solange j < länge(x.schluessel):
        neue_xk.hinzufügen(x.schluessel[j])
        neue_xv.hinzufügen(x.werte[j])
        setze j auf j + 1
    setze j auf 0
    solange j <= i:
        neue_xc.hinzufügen(x.kinder[j])
        setze j auf j + 1
    neue_xc.hinzufügen(z)
    setze j auf i + 1
    solange j < länge(x.kinder):
        neue_xc.hinzufügen(x.kinder[j])
        setze j auf j + 1
    setze x.schluessel auf neue_xk
    setze x.werte auf neue_xv
    setze x.kinder auf neue_xc

# Einfuegen in einen nicht-vollen Knoten
funktion btree_insert_nonfull(x, k, v):
    setze i auf länge(x.schluessel) - 1
    wenn x.blatt:
        # sortiert einfuegen
        setze idx auf knoten_suchindex(x, k)
        wenn idx < länge(x.schluessel) und x.schluessel[idx] == k:
            # update
            x.werte[idx] = v
            gib_zurück nichts
        setze neue_k auf []
        setze neue_v auf []
        setze j auf 0
        solange j < idx:
            neue_k.hinzufügen(x.schluessel[j])
            neue_v.hinzufügen(x.werte[j])
            setze j auf j + 1
        neue_k.hinzufügen(k)
        neue_v.hinzufügen(v)
        setze j auf idx
        solange j < länge(x.schluessel):
            neue_k.hinzufügen(x.schluessel[j])
            neue_v.hinzufügen(x.werte[j])
            setze j auf j + 1
        setze x.schluessel auf neue_k
        setze x.werte auf neue_v
        gib_zurück nichts
    # Interior: Kind finden
    setze idx auf knoten_suchindex(x, k)
    wenn idx < länge(x.schluessel) und x.schluessel[idx] == k:
        x.werte[idx] = v
        gib_zurück nichts
    wenn länge(x.kinder[idx].schluessel) == MAX_KEYS:
        btree_split_child(x, idx)
        wenn x.schluessel[idx] < k:
            setze idx auf idx + 1
        wenn x.schluessel[idx] == k:
            x.werte[idx] = v
            gib_zurück nichts
    btree_insert_nonfull(x.kinder[idx], k, v)

# Range-Query: In-Order-Traversal, nur keys in [lo, hi]
funktion btree_bereich(knoten, lo, hi, treffer):
    setze i auf 0
    solange i < länge(knoten.schluessel):
        wenn knoten.blatt == falsch:
            btree_bereich(knoten.kinder[i], lo, hi, treffer)
        setze k auf knoten.schluessel[i]
        wenn k >= lo und k <= hi:
            setze paar auf {}
            paar["schluessel"] = k
            paar["wert"] = knoten.werte[i]
            treffer.hinzufügen(paar)
        setze i auf i + 1
    wenn knoten.blatt == falsch:
        btree_bereich(knoten.kinder[länge(knoten.schluessel)], lo, hi, treffer)

# Alle keys des Baumes (sortiert) — fuer Tests
funktion btree_alle(knoten, treffer):
    setze i auf 0
    solange i < länge(knoten.schluessel):
        wenn knoten.blatt == falsch:
            btree_alle(knoten.kinder[i], treffer)
        setze paar auf {}
        paar["schluessel"] = knoten.schluessel[i]
        paar["wert"] = knoten.werte[i]
        treffer.hinzufügen(paar)
        setze i auf i + 1
    wenn knoten.blatt == falsch:
        btree_alle(knoten.kinder[länge(knoten.schluessel)], treffer)

klasse BTree:
    funktion erstelle():
        selbst.wurzel = neu BTreeKnoten(wahr)

    funktion put(k, v):
        setze r auf selbst.wurzel
        wenn länge(r.schluessel) == MAX_KEYS:
            setze s auf neu BTreeKnoten(falsch)
            s.kinder.hinzufügen(r)
            btree_split_child(s, 0)
            setze selbst.wurzel auf s
            btree_insert_nonfull(s, k, v)
        sonst:
            btree_insert_nonfull(r, k, v)

    funktion get(k):
        gib_zurück btree_suche(selbst.wurzel, k)

    funktion scan(lo, hi):
        setze treffer auf []
        btree_bereich(selbst.wurzel, lo, hi, treffer)
        gib_zurück treffer

    funktion dump():
        setze treffer auf []
        btree_alle(selbst.wurzel, treffer)
        gib_zurück treffer

# ------------------------------------------------------------
# Operation-Klassen (Vererbung fuer WAL-Serialisierung)
# ------------------------------------------------------------

klasse Operation:
    funktion erstelle(tabelle):
        selbst.tabelle = tabelle
        selbst.typ = "basis"

    funktion zu_dict():
        setze d auf {}
        d["typ"] = selbst.typ
        d["tabelle"] = selbst.tabelle
        gib_zurück d

klasse InsertOp(Operation):
    funktion erstelle(tabelle, schluessel, daten):
        selbst.tabelle = tabelle
        selbst.typ = "insert"
        selbst.schluessel = schluessel
        selbst.daten = daten

    funktion zu_dict():
        setze d auf {}
        d["typ"] = "insert"
        d["tabelle"] = selbst.tabelle
        d["schluessel"] = selbst.schluessel
        d["daten"] = selbst.daten
        gib_zurück d

klasse DeleteOp(Operation):
    funktion erstelle(tabelle, schluessel):
        selbst.tabelle = tabelle
        selbst.typ = "delete"
        selbst.schluessel = schluessel

    funktion zu_dict():
        setze d auf {}
        d["typ"] = "delete"
        d["tabelle"] = selbst.tabelle
        d["schluessel"] = selbst.schluessel
        gib_zurück d

klasse CreateOp(Operation):
    funktion erstelle(tabelle, schema):
        selbst.tabelle = tabelle
        selbst.typ = "create"
        selbst.schema = schema

    funktion zu_dict():
        setze d auf {}
        d["typ"] = "create"
        d["tabelle"] = selbst.tabelle
        d["schema"] = selbst.schema
        gib_zurück d

# ------------------------------------------------------------
# Tabelle
# ------------------------------------------------------------

klasse Tabelle:
    funktion erstelle(name, schema):
        selbst.name = name
        selbst.schema = schema     # Liste<[col_name, col_typ]>
        selbst.index = neu BTree()
        selbst.tombstones = {}     # schluessel -> wahr (geloescht)

    funktion ist_geloescht(k):
        wenn selbst.tombstones.hat(text(k)):
            gib_zurück wahr
        gib_zurück falsch

    funktion einfuegen_intern(k, daten):
        selbst.index.einfuegen(k, daten)
        wenn selbst.tombstones.hat(text(k)):
            # un-tombstone
            setze neu_ts auf {}
            setze keys auf selbst.tombstones.schlüssel()
            setze i auf 0
            solange i < länge(keys):
                wenn keys[i] != text(k):
                    neu_ts[keys[i]] = wahr
                setze i auf i + 1
            setze selbst.tombstones auf neu_ts

    funktion loeschen_intern(k):
        selbst.tombstones[text(k)] = wahr

    funktion suche(k):
        wenn selbst.ist_geloescht(k):
            gib_zurück nichts
        gib_zurück selbst.index.suche(k)

    funktion bereich(lo, hi):
        setze roh auf selbst.index.bereich(lo, hi)
        setze gefiltert auf []
        setze i auf 0
        solange i < länge(roh):
            wenn selbst.ist_geloescht(roh[i]["schluessel"]) == falsch:
                gefiltert.hinzufügen(roh[i])
            setze i auf i + 1
        gib_zurück gefiltert

    funktion anzahl():
        setze alle auf selbst.index.alle()
        setze n auf 0
        setze i auf 0
        solange i < länge(alle):
            wenn selbst.ist_geloescht(alle[i]["schluessel"]) == falsch:
                setze n auf n + 1
            setze i auf i + 1
        gib_zurück n

# ------------------------------------------------------------
# Datenbank
# ------------------------------------------------------------

klasse Datenbank:
    funktion erstelle(wal_pfad):
        selbst.tabellen = {}
        selbst.wal_pfad = wal_pfad
        selbst.recovery = falsch   # waehrend replay keine neuen Log-Eintraege

    funktion wal_schreiben(op):
        wenn selbst.recovery:
            gib_zurück nichts
        setze zeile auf json_string(op.zu_dict()) + "\n"
        datei_anhängen(selbst.wal_pfad, zeile)

    funktion tabelle_erstellen(name, schema):
        wenn selbst.tabellen.hat(name):
            zeige "Tabelle existiert bereits: " + name
            gib_zurück nichts
        selbst.tabellen[name] = neu Tabelle(name, schema)
        selbst.wal_schreiben(neu CreateOp(name, schema))

    funktion einfuegen(tabelle_name, k, daten):
        wenn selbst.tabellen.hat(tabelle_name) == falsch:
            zeige "Unbekannte Tabelle: " + tabelle_name
            gib_zurück nichts
        setze t auf selbst.tabellen[tabelle_name]
        t.einfuegen_intern(k, daten)
        selbst.wal_schreiben(neu InsertOp(tabelle_name, k, daten))

    funktion loeschen(tabelle_name, k):
        wenn selbst.tabellen.hat(tabelle_name) == falsch:
            gib_zurück nichts
        setze t auf selbst.tabellen[tabelle_name]
        t.loeschen_intern(k)
        selbst.wal_schreiben(neu DeleteOp(tabelle_name, k))

    funktion suche(tabelle_name, k):
        wenn selbst.tabellen.hat(tabelle_name) == falsch:
            gib_zurück nichts
        gib_zurück selbst.tabellen[tabelle_name].suche(k)

    funktion bereich(tabelle_name, lo, hi):
        wenn selbst.tabellen.hat(tabelle_name) == falsch:
            gib_zurück []
        gib_zurück selbst.tabellen[tabelle_name].bereich(lo, hi)

    funktion recovery_aus_wal():
        wenn datei_existiert(selbst.wal_pfad) == falsch:
            gib_zurück nichts
        setze selbst.recovery auf wahr
        setze inhalt auf datei_lesen(selbst.wal_pfad)
        setze zeilen auf inhalt.teilen("\n")
        setze i auf 0
        solange i < länge(zeilen):
            setze z auf zeilen[i]
            wenn länge(z) > 0:
                setze op auf json_parse(z)
                setze typ auf op["typ"]
                wenn typ == "create":
                    selbst.tabelle_erstellen(op["tabelle"], op["schema"])
                sonst wenn typ == "insert":
                    selbst.einfuegen(op["tabelle"], op["schluessel"], op["daten"])
                sonst wenn typ == "delete":
                    selbst.loeschen(op["tabelle"], op["schluessel"])
            setze i auf i + 1
        setze selbst.recovery auf falsch

# ------------------------------------------------------------
# Test-Framework
# ------------------------------------------------------------

setze zaehler auf {}
zaehler["gesamt"] = 0
zaehler["ok"] = 0

funktion check(name, bedingung):
    zaehler["gesamt"] = zaehler["gesamt"] + 1
    wenn bedingung:
        zaehler["ok"] = zaehler["ok"] + 1
        zeige "  OK    " + name
    sonst:
        zeige "  FAIL  " + name

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo Mini-DB — B-Tree + WAL Test-Suite"
zeige "================================================"

# Test-Setup: altes WAL loeschen
wenn datei_existiert("mini_db.wal"):
    datei_löschen("mini_db.wal")

zeige ""
zeige "--- B-Tree Grundlagen ---"

setze baum auf neu BTree()
baum.einfuegen(10, "zehn")
baum.einfuegen(5, "fuenf")
baum.einfuegen(20, "zwanzig")
baum.einfuegen(15, "fuenfzehn")
baum.einfuegen(3, "drei")
baum.einfuegen(8, "acht")
baum.einfuegen(25, "fuenfundzwanzig")

check("suche(10)", baum.suche(10) == "zehn")
check("suche(5)", baum.suche(5) == "fuenf")
check("suche(25)", baum.suche(25) == "fuenfundzwanzig")
check("suche(99) = nichts", baum.suche(99) == nichts)

setze alle auf baum.alle()
check("alle() liefert 7 keys", länge(alle) == 7)
check("alle() sortiert: erster=3", alle[0]["schluessel"] == 3)
check("alle() sortiert: letzter=25", alle[6]["schluessel"] == 25)

setze ber auf baum.bereich(8, 20)
check("bereich(8,20) liefert 4 keys", länge(ber) == 4)
check("bereich first=8", ber[0]["schluessel"] == 8)
check("bereich last=20", ber[3]["schluessel"] == 20)

zeige ""
zeige "--- B-Tree Stresstest: 1000 Inserts ---"

setze gross auf neu BTree()
setze i auf 0
solange i < 1000:
    gross.einfuegen(i, "wert_" + text(i))
    setze i auf i + 1

check("1000 Inserts: alle() = 1000", länge(gross.alle()) == 1000)
check("1000: suche(0)", gross.suche(0) == "wert_0")
check("1000: suche(500)", gross.suche(500) == "wert_500")
check("1000: suche(999)", gross.suche(999) == "wert_999")
check("1000: suche(1000) = nichts", gross.suche(1000) == nichts)

setze range500_600 auf gross.bereich(500, 600)
check("1000: bereich(500,600) = 101", länge(range500_600) == 101)

# zufaellige Reihenfolge
setze gross2 auf neu BTree()
setze reihenfolge auf [743, 12, 999, 1, 500, 250, 750, 333, 666, 100]
setze i auf 0
solange i < länge(reihenfolge):
    gross2.einfuegen(reihenfolge[i], "x")
    setze i auf i + 1
setze alle2 auf gross2.alle()
check("zufaellig sortiert: first=1", alle2[0]["schluessel"] == 1)
check("zufaellig sortiert: last=999", alle2[länge(alle2) - 1]["schluessel"] == 999)

# Update-Test
gross.einfuegen(500, "neuer_wert")
check("Update: suche(500)", gross.suche(500) == "neuer_wert")

zeige ""
zeige "--- Datenbank + Tabellen ---"

setze db auf neu Datenbank("mini_db.wal")
db.tabelle_erstellen("users", [["id", "int"], ["name", "text"], ["age", "int"]])

setze u1 auf {}
u1["name"] = "Alice"
u1["age"] = 30
db.einfuegen("users", 1, u1)

setze u2 auf {}
u2["name"] = "Bob"
u2["age"] = 25
db.einfuegen("users", 2, u2)

setze u3 auf {}
u3["name"] = "Claire"
u3["age"] = 35
db.einfuegen("users", 3, u3)

check("users anzahl = 3", db.tabellen["users"].anzahl() == 3)
check("suche(1) name = Alice", db.suche("users", 1)["name"] == "Alice")
check("suche(2) age = 25", db.suche("users", 2)["age"] == 25)

db.loeschen("users", 2)
check("nach delete: anzahl = 2", db.tabellen["users"].anzahl() == 2)
check("nach delete: suche(2) = nichts", db.suche("users", 2) == nichts)
check("suche(1) noch da", db.suche("users", 1)["name"] == "Alice")

# Bereich
setze bereich auf db.bereich("users", 1, 10)
check("bereich(users,1,10) = 2", länge(bereich) == 2)

zeige ""
zeige "--- WAL Recovery ---"

# Neue DB-Instanz, replay aus WAL
setze db2 auf neu Datenbank("mini_db.wal")
db2.recovery_aus_wal()

check("Recovery: users existiert", db2.tabellen.hat("users"))
check("Recovery: anzahl = 2", db2.tabellen["users"].anzahl() == 2)
check("Recovery: suche(1)", db2.suche("users", 1)["name"] == "Alice")
check("Recovery: suche(2) = nichts (tombstone)", db2.suche("users", 2) == nichts)
check("Recovery: suche(3)", db2.suche("users", 3)["name"] == "Claire")

zeige ""
zeige "--- WAL Massentest ---"

wenn datei_existiert("mini_db2.wal"):
    datei_löschen("mini_db2.wal")

setze db3 auf neu Datenbank("mini_db2.wal")
db3.tabelle_erstellen("logs", [["id", "int"], ["msg", "text"]])

setze i auf 0
solange i < 200:
    setze r auf {}
    r["msg"] = "log eintrag " + text(i)
    db3.einfuegen("logs", i, r)
    setze i auf i + 1

# Lösche jeden 10.
setze i auf 0
solange i < 200:
    wenn i % 10 == 0:
        db3.loeschen("logs", i)
    setze i auf i + 1

check("Massentest: 200 - 20 = 180", db3.tabellen["logs"].anzahl() == 180)

# Recovery
setze db4 auf neu Datenbank("mini_db2.wal")
db4.recovery_aus_wal()
check("Recovery Mass: 180 nach replay", db4.tabellen["logs"].anzahl() == 180)
check("Recovery Mass: suche(5) noch da", db4.suche("logs", 5)["msg"] == "log eintrag 5")
check("Recovery Mass: suche(10) geloescht", db4.suche("logs", 10) == nichts)
check("Recovery Mass: suche(100) geloescht", db4.suche("logs", 100) == nichts)
check("Recovery Mass: suche(199) noch da", db4.suche("logs", 199)["msg"] == "log eintrag 199")

# Cleanup
datei_löschen("mini_db.wal")
datei_löschen("mini_db2.wal")

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"

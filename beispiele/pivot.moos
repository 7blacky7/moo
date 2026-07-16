# CSV-Pivot-Table — Stresstest fuer Daten-Aggregation
# Input:  sales.csv (date,product,region,amount)
# Output: Pivot-Table rows=product, cols=region, values=sum(amount)
#         mit Total-Spalte + Total-Zeile, Markdown-Format
# Zusatz: Filter (year=2024), Sort by total desc, Top-N

klasse Pivot:
    funktion erstelle():
        selbst.zeilen = []
        selbst.products = []
        selbst.regions = []
        selbst.grid = {}
        selbst.row_total = {}
        selbst.col_total = {}
        selbst.grand = 0

    funktion lade_csv(pfad):
        setze lines auf datei_zeilen(pfad)
        setze kopf auf lines[0].teilen(",")
        setze idx auf 1
        solange idx < länge(lines):
            setze l auf lines[idx]
            setze trimmed auf l.trim()
            wenn länge(trimmed) > 0:
                setze teile auf trimmed.teilen(",")
                setze zeile auf {
                    "date": teile[0],
                    "product": teile[1],
                    "region": teile[2],
                    "amount": zahl(teile[3])
                }
                selbst.zeilen.hinzufügen(zeile)
            setze idx auf idx + 1

    funktion filter_year(jahr):
        setze neu_zeilen auf []
        für z in selbst.zeilen:
            wenn z["date"].slice(0, 4) == jahr:
                neu_zeilen.hinzufügen(z)
        selbst.zeilen = neu_zeilen

    funktion baue():
        selbst.products = []
        selbst.regions = []
        selbst.grid = {}
        selbst.row_total = {}
        selbst.col_total = {}
        selbst.grand = 0

        für z in selbst.zeilen:
            setze p auf z["product"]
            setze r auf z["region"]
            setze a auf z["amount"]

            wenn selbst.grid.hat(p) == falsch:
                selbst.grid[p] = {}
                selbst.products.hinzufügen(p)
                selbst.row_total[p] = 0

            setze zelle auf selbst.grid[p]
            wenn zelle.hat(r):
                zelle[r] = zelle[r] + a
            sonst:
                zelle[r] = a
            selbst.grid[p] = zelle

            wenn selbst.col_total.hat(r):
                selbst.col_total[r] = selbst.col_total[r] + a
            sonst:
                selbst.col_total[r] = a
                selbst.regions.hinzufügen(r)

            selbst.row_total[p] = selbst.row_total[p] + a
            selbst.grand = selbst.grand + a

        selbst.sortiere_regions()

    # Alphabetische Region-Sortierung fuer stabile Ausgabe
    funktion sortiere_regions():
        setze rs auf selbst.regions
        setze n auf länge(rs)
        setze idx auf 1
        solange idx < n:
            setze jdx auf idx
            solange jdx > 0:
                wenn rs[jdx - 1] > rs[jdx]:
                    setze tmp auf rs[jdx - 1]
                    rs[jdx - 1] = rs[jdx]
                    rs[jdx] = tmp
                    setze jdx auf jdx - 1
                sonst:
                    stopp
            setze idx auf idx + 1
        selbst.regions = rs

    # Sortiert products nach row_total DESC
    funktion sort_by_total():
        setze ps auf selbst.products
        setze n auf länge(ps)
        setze idx auf 1
        solange idx < n:
            setze jdx auf idx
            solange jdx > 0:
                setze a auf selbst.row_total[ps[jdx - 1]]
                setze b auf selbst.row_total[ps[jdx]]
                wenn a < b:
                    setze tmp auf ps[jdx - 1]
                    ps[jdx - 1] = ps[jdx]
                    ps[jdx] = tmp
                    setze jdx auf jdx - 1
                sonst:
                    stopp
            setze idx auf idx + 1
        selbst.products = ps

    funktion top_n(n):
        wenn länge(selbst.products) <= n:
            gib_zurück nichts
        setze neu_ps auf []
        setze idx auf 0
        solange idx < n:
            neu_ps.hinzufügen(selbst.products[idx])
            setze idx auf idx + 1
        selbst.products = neu_ps

    funktion als_markdown():
        setze zeilen_str auf []
        setze kopf auf "| Product |"
        für r in selbst.regions:
            setze kopf auf kopf + " " + r + " |"
        setze kopf auf kopf + " Total |"
        zeilen_str.hinzufügen(kopf)

        setze sep auf "|---|"
        für r in selbst.regions:
            setze sep auf sep + "---|"
        setze sep auf sep + "---|"
        zeilen_str.hinzufügen(sep)

        für p in selbst.products:
            setze zeile auf "| " + p + " |"
            setze zelle auf selbst.grid[p]
            für r in selbst.regions:
                wenn zelle.hat(r):
                    setze zeile auf zeile + " " + text(zelle[r]) + " |"
                sonst:
                    setze zeile auf zeile + " 0 |"
            setze zeile auf zeile + " " + text(selbst.row_total[p]) + " |"
            zeilen_str.hinzufügen(zeile)

        setze total_zeile auf "| **Total** |"
        für r in selbst.regions:
            wenn selbst.col_total.hat(r):
                setze total_zeile auf total_zeile + " " + text(selbst.col_total[r]) + " |"
            sonst:
                setze total_zeile auf total_zeile + " 0 |"
        setze total_zeile auf total_zeile + " " + text(selbst.grand) + " |"
        zeilen_str.hinzufügen(total_zeile)

        gib_zurück zeilen_str.verbinden("\n")


# ============================================================
# TEST-DATEN GENERIEREN (deterministisch)
# ============================================================

funktion erzeuge_csv(pfad):
    setze inhalt auf "date,product,region,amount\n"
    setze products auf ["Apfel", "Banane", "Kirsche", "Dattel", "Orange"]
    setze regions auf ["Nord", "Sued", "Ost", "West"]
    setze years auf ["2023", "2024", "2025"]
    setze idx auf 0
    solange idx < 240:
        setze p auf products[idx % 5]
        setze r auf regions[idx % 4]
        setze y auf years[idx % 3]
        setze monat auf (idx % 12) + 1
        setze mstr auf text(monat)
        wenn monat < 10:
            setze mstr auf "0" + mstr
        setze date auf y + "-" + mstr + "-15"
        setze amount auf ((idx * 7) % 100) + 10
        setze zeile auf date + "," + p + "," + r + "," + text(amount) + "\n"
        setze inhalt auf inhalt + zeile
        setze idx auf idx + 1
    datei_schreiben(pfad, inhalt)


# ============================================================
# TESTS
# ============================================================

klasse Zaehler:
    funktion erstelle():
        selbst.ok = 0
        selbst.fail = 0

    funktion erfolg():
        selbst.ok = selbst.ok + 1

    funktion fehler():
        selbst.fail = selbst.fail + 1

setze TESTS auf neu Zaehler()

funktion check(name, bedingung):
    wenn bedingung:
        zeige "  OK  " + name
        TESTS.erfolg()
    sonst:
        zeige "  FAIL " + name
        TESTS.fehler()


zeige "=== CSV-Pivot-Table Tests ==="
zeige ""

# 0. Test-CSV schreiben
erzeuge_csv("/tmp/sales.csv")
check("sales.csv geschrieben", datei_existiert("/tmp/sales.csv"))

# 1. CSV laden
zeige ""
zeige "Test 1: CSV laden"
setze p auf neu Pivot()
p.lade_csv("/tmp/sales.csv")
check("240 Zeilen", länge(p.zeilen) == 240)
check("erste Zeile hat date-Feld", p.zeilen[0]["date"].slice(0, 4) == "2023")

# 2. Pivot bauen (ohne Filter)
zeige ""
zeige "Test 2: Pivot bauen"
p.baue()
check("5 Produkte", länge(p.products) == 5)
check("4 Regionen", länge(p.regions) == 4)
check("Regionen sortiert (Nord zuerst)", p.regions[0] == "Nord")
# Summe der col_totals == grand
setze summe auf 0
für r in p.regions:
    setze summe auf summe + p.col_total[r]
check("col_totals == grand", summe == p.grand)

setze summe2 auf 0
für pr in p.products:
    setze summe2 auf summe2 + p.row_total[pr]
check("row_totals == grand", summe2 == p.grand)

# 3. Filter auf Jahr 2024
zeige ""
zeige "Test 3: Filter year=2024"
setze p2 auf neu Pivot()
p2.lade_csv("/tmp/sales.csv")
p2.filter_year("2024")
p2.baue()
check("weniger Zeilen nach Filter", länge(p2.zeilen) < 240)
check("alle aus 2024", p2.zeilen[0]["date"].slice(0, 4) == "2024")
check("immer noch 5 Produkte", länge(p2.products) == 5)

# 4. Sort by total desc
zeige ""
zeige "Test 4: Sort by total desc"
p.sort_by_total()
setze erster auf p.products[0]
setze letzter auf p.products[länge(p.products) - 1]
check("erster hat hoechstes total", p.row_total[erster] >= p.row_total[letzter])

# 5. Top-N
zeige ""
zeige "Test 5: Top-3"
p.top_n(3)
check("3 Produkte uebrig", länge(p.products) == 3)

# 6. Markdown-Ausgabe
zeige ""
zeige "Test 6: Markdown-Ausgabe"
setze md auf p.als_markdown()
check("Markdown nicht leer", länge(md) > 0)
check("enthaelt Header-Zeile", md.slice(0, 10) == "| Product ")
check("enthaelt Total-Zeile", md.ersetzen("Total", "X") != md)

zeige ""
zeige "--- Pivot-Tabelle (Top-3 nach Total) ---"
zeige md
zeige "--- Ende ---"

# 7. Vollstaendige Pivot (ohne Top-N) und deterministische Summen
zeige ""
zeige "Test 7: deterministische Summen"
setze p3 auf neu Pivot()
p3.lade_csv("/tmp/sales.csv")
p3.baue()
# amount = ((idx * 7) % 100) + 10 fuer idx=0..239
# Manuell berechnen: Summe = Sum(((i*7)%100)+10 for i in 0..239)
setze erwartet auf 0
setze k auf 0
solange k < 240:
    setze erwartet auf erwartet + ((k * 7) % 100) + 10
    setze k auf k + 1
check("grand total korrekt", p3.grand == erwartet)

# 8. Eine spezifische Zelle pruefen
zeige ""
zeige "Test 8: spezifische Zelle pruefen"
# Apfel * idx%5==0 → idx in {0,5,10,...,235} = 48 Zeilen
# Davon region je nach idx%4:
# idx=0: region[0]=Nord, idx=5: region[1]=Sued, idx=10: region[2]=Ost, idx=15: region[3]=West
# idx=20: Nord, ...
# Apfel-Zeilen: idx 0,5,10,15,20,25,... = 48 Eintraege
# Regionen zyklisch: Nord,Sued,Ost,West,Nord,... (48/4 = 12 je Region)
check("Apfel existiert", p3.grid.hat("Apfel"))
setze apfel auf p3.grid["Apfel"]
check("Apfel hat 4 Regionen", länge(apfel.schlüssel()) == 4)

# Exact Apfel-Nord sum: idx in {0,20,40,60,80,100,120,140,160,180,200,220}
setze apfel_nord auf 0
setze k auf 0
solange k < 240:
    wenn k % 5 == 0 und k % 4 == 0:
        setze apfel_nord auf apfel_nord + ((k * 7) % 100) + 10
    setze k auf k + 1
check("Apfel/Nord exakt", apfel["Nord"] == apfel_nord)

zeige ""
zeige "=========================================="
zeige "Ergebnis: " + text(TESTS.ok) + " OK, " + text(TESTS.fail) + " FAIL"
zeige "=========================================="

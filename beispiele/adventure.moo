# ============================================================
# moo Text-Adventure-Engine: "Die verlorene Krone von Aerandur"
#
# Features:
#   - 10 Raeume, 5+ Items (mit Vererbung), 3 NPCs
#   - Parser: gehe, nimm, benutze, schau, inventar, rede, greife_an,
#             speichern, laden, hilfe, beenden
#   - Kampfsystem (HP / Angriff / Verteidigung)
#   - Quest-Flags (Dict)
#   - JSON Save/Load
# ============================================================

# ------------------------------------------------------------
# Item-Hierarchie (Vererbung + Dynamic Dispatch)
# ------------------------------------------------------------

klasse Item:
    funktion erstelle(name, beschreibung):
        selbst.name = name
        selbst.beschreibung = beschreibung
        selbst.typ = "item"

    funktion benutze(spieler, welt):
        zeige "Du weisst nicht, wie du " + selbst.name + " benutzen sollst."
        gib_zurück falsch

klasse Waffe(Item):
    funktion erstelle(name, beschreibung, schaden):
        selbst.name = name
        selbst.beschreibung = beschreibung
        selbst.typ = "waffe"
        selbst.schaden = schaden

    funktion benutze(spieler, welt):
        spieler["waffe"] = selbst.name
        spieler["schaden_bonus"] = selbst.schaden
        zeige "Du ziehst " + selbst.name + ". (+" + text(selbst.schaden) + " Schaden)"
        gib_zurück wahr

klasse Trank(Item):
    funktion erstelle(name, beschreibung, heilung):
        selbst.name = name
        selbst.beschreibung = beschreibung
        selbst.typ = "trank"
        selbst.heilung = heilung

    funktion benutze(spieler, welt):
        setze alt auf spieler["hp"]
        setze neu_hp auf alt + selbst.heilung
        wenn neu_hp > spieler["hp_max"]:
            setze neu_hp auf spieler["hp_max"]
        spieler["hp"] = neu_hp
        zeige "Du trinkst " + selbst.name + ". HP: " + text(alt) + " -> " + text(neu_hp)
        setze inv auf spieler["inventar"]
        setze neue_inv auf []
        setze verbraucht auf falsch
        setze i auf 0
        solange i < länge(inv):
            wenn inv[i] == selbst.name und verbraucht == falsch:
                setze verbraucht auf wahr
            sonst:
                neue_inv.hinzufügen(inv[i])
            setze i auf i + 1
        spieler["inventar"] = neue_inv
        gib_zurück wahr

klasse Schluessel(Item):
    funktion erstelle(name, beschreibung, oeffnet):
        selbst.name = name
        selbst.beschreibung = beschreibung
        selbst.typ = "schluessel"
        selbst.oeffnet = oeffnet

    funktion benutze(spieler, welt):
        setze raum auf welt["raeume"][spieler["ort"]]
        # Pruefe alle angrenzenden Raeume ob einer mit diesem Schluessel offen wird
        setze ausg auf raum["ausgaenge"]
        setze richtungen auf ausg.schlüssel()
        setze i auf 0
        setze geoeffnet auf falsch
        solange i < länge(richtungen):
            setze ziel auf ausg[richtungen[i]]
            setze zraum auf welt["raeume"][ziel]
            wenn zraum["gesperrt"] != nichts und zraum["gesperrt"] == selbst.oeffnet:
                zraum["gesperrt"] = nichts
                zeige "Du oeffnest " + zraum["name"] + " mit " + selbst.name + "."
                welt["flags"][selbst.oeffnet + "_offen"] = wahr
                setze geoeffnet auf wahr
            setze i auf i + 1
        wenn geoeffnet:
            gib_zurück wahr
        zeige selbst.name + " passt hier nicht."
        gib_zurück falsch

klasse Fackel(Item):
    funktion erstelle():
        selbst.name = "fackel"
        selbst.beschreibung = "Eine flackernde Fackel."
        selbst.typ = "fackel"

    funktion benutze(spieler, welt):
        spieler["licht"] = wahr
        welt["flags"]["fackel_an"] = wahr
        zeige "Die Fackel flammt auf. Dunkle Raeume sind jetzt sichtbar."
        gib_zurück wahr

klasse Amulett(Item):
    funktion erstelle():
        selbst.name = "amulett"
        selbst.beschreibung = "Ein glaenzendes Amulett mit dem Symbol von Aerandur."
        selbst.typ = "amulett"

    funktion benutze(spieler, welt):
        zeige "Das Amulett glueht in deiner Hand."
        gib_zurück wahr

# ------------------------------------------------------------
# NPCs (auch mit Vererbung)
# ------------------------------------------------------------

klasse NPC:
    funktion erstelle(name, dialog):
        selbst.name = name
        selbst.dialog = dialog
        selbst.freundlich = wahr
        selbst.hp = 0
        selbst.schaden = 0

    funktion rede(spieler, welt):
        zeige selbst.name + ": \"" + selbst.dialog + "\""

klasse Haendler(NPC):
    funktion erstelle():
        selbst.name = "haendler"
        selbst.dialog = "Willkommen Reisender!"
        selbst.freundlich = wahr
        selbst.hp = 0
        selbst.schaden = 0

    funktion rede(spieler, welt):
        zeige "Haendler: \"Fuer 5 Gold gibt's einen Heiltrank. (Befehl: tausche)\""

klasse Waechter(NPC):
    funktion erstelle():
        selbst.name = "waechter"
        selbst.dialog = "Niemand passiert ohne das Amulett von Aerandur!"
        selbst.freundlich = falsch
        selbst.hp = 30
        selbst.schaden = 6

    funktion rede(spieler, welt):
        wenn welt["flags"]["amulett_gezeigt"]:
            zeige "Waechter: \"Das Amulett! Tretet ein, Auserwaehlte.\""
        sonst:
            zeige "Waechter: \"" + selbst.dialog + "\""

klasse Drache(NPC):
    funktion erstelle():
        selbst.name = "drache"
        selbst.dialog = "KRRROAAAARRR!"
        selbst.freundlich = falsch
        selbst.hp = 40
        selbst.schaden = 10

    funktion rede(spieler, welt):
        zeige "Der Drache speit Feuer. Reden ist sinnlos — greife_an oder fliehe!"

# ------------------------------------------------------------
# Welt-Konstruktion
# ------------------------------------------------------------

funktion baue_welt():
    setze welt auf {}

    # Items registrieren
    setze items auf {}
    items["schwert"] = neu Waffe("schwert", "Eine scharfe Klinge.", 8)
    items["dolch"] = neu Waffe("dolch", "Klein und schnell.", 4)
    items["heiltrank"] = neu Trank("heiltrank", "Rote Fluessigkeit.", 30)
    items["schluessel"] = neu Schluessel("schluessel", "Alter Eisenschluessel.", "verlies")
    items["fackel"] = neu Fackel()
    items["amulett"] = neu Amulett()
    items["goldbeutel"] = neu Item("goldbeutel", "Beutel mit Gold.")
    welt["items"] = items

    # NPCs registrieren
    setze npcs auf {}
    npcs["haendler"] = neu Haendler()
    npcs["waechter"] = neu Waechter()
    npcs["drache"] = neu Drache()
    welt["npcs"] = npcs

    # Raeume (alle inline gebaut)
    setze raeume auf {}

    setze r1 auf {}
    r1["name"] = "Hoehleneingang"
    r1["lang"] = "Du stehst vor einer Hoehle. Tropfen klopfen auf Stein. Sueden: heller Wald. Norden: dunkle Hoehle."
    setze ex1 auf {}
    ex1["sueden"] = "waldlichtung"
    ex1["norden"] = "dunkle_hoehle"
    r1["ausgaenge"] = ex1
    r1["items"] = ["fackel"]
    r1["npcs"] = []
    r1["dunkel"] = falsch
    r1["gesperrt"] = nichts
    raeume["hoehleneingang"] = r1

    setze r2 auf {}
    r2["name"] = "Waldlichtung"
    r2["lang"] = "Sonnige Lichtung mit einem Haendler am Karren. Wege: norden (Hoehle), osten (Fluss), westen (Dorf)."
    setze ex2 auf {}
    ex2["norden"] = "hoehleneingang"
    ex2["osten"] = "fluss"
    ex2["westen"] = "dorf"
    r2["ausgaenge"] = ex2
    r2["items"] = []
    r2["npcs"] = ["haendler"]
    r2["dunkel"] = falsch
    r2["gesperrt"] = nichts
    raeume["waldlichtung"] = r2

    setze r3 auf {}
    r3["name"] = "Dorf"
    r3["lang"] = "Verlassenes Dorf. Auf einem Tisch liegt ein Dolch. In einer Truhe: ein altes Amulett. Ausgang: osten."
    setze ex3 auf {}
    ex3["osten"] = "waldlichtung"
    r3["ausgaenge"] = ex3
    r3["items"] = ["dolch", "amulett"]
    r3["npcs"] = []
    r3["dunkel"] = falsch
    r3["gesperrt"] = nichts
    raeume["dorf"] = r3

    setze r4 auf {}
    r4["name"] = "Flussufer"
    r4["lang"] = "Reissender Fluss. Im Wasser schimmert ein Heiltrank. Bruecke nach osten. Westen: Wald."
    setze ex4 auf {}
    ex4["westen"] = "waldlichtung"
    ex4["osten"] = "bruecke"
    r4["ausgaenge"] = ex4
    r4["items"] = ["heiltrank"]
    r4["npcs"] = []
    r4["dunkel"] = falsch
    r4["gesperrt"] = nichts
    raeume["fluss"] = r4

    setze r5 auf {}
    r5["name"] = "Morsche Bruecke"
    r5["lang"] = "Wackelige Holzbruecke. Waechter versperrt den Weg nach osten. Westen: Fluss."
    setze ex5 auf {}
    ex5["westen"] = "fluss"
    ex5["osten"] = "tempel"
    r5["ausgaenge"] = ex5
    r5["items"] = []
    r5["npcs"] = ["waechter"]
    r5["dunkel"] = falsch
    r5["gesperrt"] = nichts
    raeume["bruecke"] = r5

    setze r6 auf {}
    r6["name"] = "Tempel von Aerandur"
    r6["lang"] = "Saeulen, hoher Altar. Hier lag einst die Krone. Treppen nach unten (verlies), zurueck westen (Bruecke)."
    setze ex6 auf {}
    ex6["westen"] = "bruecke"
    ex6["unten"] = "verlies"
    r6["ausgaenge"] = ex6
    r6["items"] = []
    r6["npcs"] = []
    r6["dunkel"] = falsch
    r6["gesperrt"] = "waechter"
    raeume["tempel"] = r6

    setze r7 auf {}
    r7["name"] = "Verlies"
    r7["lang"] = "Dunkles Verlies mit Ketten. Tuer nach osten ist verschlossen. Oben: Tempel."
    setze ex7 auf {}
    ex7["oben"] = "tempel"
    ex7["osten"] = "schatzkammer"
    r7["ausgaenge"] = ex7
    r7["items"] = ["schluessel"]
    r7["npcs"] = []
    r7["dunkel"] = wahr
    r7["gesperrt"] = nichts
    raeume["verlies"] = r7

    setze r8 auf {}
    r8["name"] = "Schatzkammer"
    r8["lang"] = "Gold glitzert. Ein riesiger Drache schlaeft auf einem Haufen. Ausweg: westen."
    setze ex8 auf {}
    ex8["westen"] = "verlies"
    r8["ausgaenge"] = ex8
    r8["items"] = ["goldbeutel"]
    r8["npcs"] = ["drache"]
    r8["dunkel"] = falsch
    r8["gesperrt"] = "verlies"
    raeume["schatzkammer"] = r8

    setze r9 auf {}
    r9["name"] = "Dunkle Hoehle"
    r9["lang"] = "Stockfinster. (Tipp: benutze fackel). Tiefer: tiefen. Zurueck: sueden."
    setze ex9 auf {}
    ex9["sueden"] = "hoehleneingang"
    ex9["tiefen"] = "hoehlenkammer"
    r9["ausgaenge"] = ex9
    r9["items"] = ["schwert"]
    r9["npcs"] = []
    r9["dunkel"] = wahr
    r9["gesperrt"] = nichts
    raeume["dunkle_hoehle"] = r9

    setze r10 auf {}
    r10["name"] = "Hoehlenkammer"
    r10["lang"] = "Runde Kammer mit Stalaktiten. Zurueck: sueden."
    setze ex10 auf {}
    ex10["sueden"] = "dunkle_hoehle"
    r10["ausgaenge"] = ex10
    r10["items"] = []
    r10["npcs"] = []
    r10["dunkel"] = falsch
    r10["gesperrt"] = nichts
    raeume["hoehlenkammer"] = r10

    welt["raeume"] = raeume

    # Quest-Flags
    setze flags auf {}
    flags["amulett_gezeigt"] = falsch
    flags["waechter_besiegt"] = falsch
    flags["drache_besiegt"] = falsch
    flags["verlies_offen"] = falsch
    flags["fackel_an"] = falsch
    welt["flags"] = flags

    gib_zurück welt

# ------------------------------------------------------------
# Spieler-Zustand
# ------------------------------------------------------------

funktion neuer_spieler():
    setze p auf {}
    p["ort"] = "hoehleneingang"
    p["hp"] = 80
    p["hp_max"] = 80
    p["schaden_bonus"] = 2
    p["waffe"] = "faeuste"
    p["inventar"] = []
    p["licht"] = falsch
    p["gold"] = 3
    gib_zurück p

# ------------------------------------------------------------
# Ausgabe-Helfer
# ------------------------------------------------------------

funktion zeige_raum(welt, spieler):
    setze raum auf welt["raeume"][spieler["ort"]]
    zeige ""
    zeige "=== " + raum["name"] + " ==="
    wenn raum["dunkel"] und spieler["licht"] == falsch:
        zeige "Es ist stockfinster. Du siehst nur die Umrisse von Ausgaengen."
    sonst:
        zeige raum["lang"]
        wenn länge(raum["items"]) > 0:
            setze s auf "Gegenstaende: "
            setze i auf 0
            solange i < länge(raum["items"]):
                wenn i > 0:
                    setze s auf s + ", "
                setze s auf s + raum["items"][i]
                setze i auf i + 1
            zeige s
        wenn länge(raum["npcs"]) > 0:
            setze s2 auf "Personen: "
            setze j auf 0
            solange j < länge(raum["npcs"]):
                wenn j > 0:
                    setze s2 auf s2 + ", "
                setze s2 auf s2 + raum["npcs"][j]
                setze j auf j + 1
            zeige s2
    setze ausg auf raum["ausgaenge"]
    setze schluessel_liste auf ausg.schlüssel()
    setze sa auf "Ausgaenge: "
    setze k auf 0
    solange k < länge(schluessel_liste):
        wenn k > 0:
            setze sa auf sa + ", "
        setze sa auf sa + schluessel_liste[k]
        setze k auf k + 1
    zeige sa

funktion zeige_status(spieler):
    zeige "HP: " + text(spieler["hp"]) + "/" + text(spieler["hp_max"]) + " | Waffe: " + spieler["waffe"] + " | Gold: " + text(spieler["gold"])

# ------------------------------------------------------------
# Inventar-Hilfen
# ------------------------------------------------------------

funktion hat_item(spieler, name):
    setze i auf 0
    solange i < länge(spieler["inventar"]):
        wenn spieler["inventar"][i] == name:
            gib_zurück wahr
        setze i auf i + 1
    gib_zurück falsch

funktion entferne_aus_liste(liste, name):
    setze neu_liste auf []
    setze entfernt auf falsch
    setze i auf 0
    solange i < länge(liste):
        wenn liste[i] == name und entfernt == falsch:
            setze entfernt auf wahr
        sonst:
            neu_liste.hinzufügen(liste[i])
        setze i auf i + 1
    gib_zurück neu_liste

# ------------------------------------------------------------
# Befehle
# ------------------------------------------------------------

funktion cmd_gehen(welt, spieler, richtung):
    setze raum auf welt["raeume"][spieler["ort"]]
    wenn raum["ausgaenge"].hat(richtung) == falsch:
        zeige "Dort gibt es keinen Ausgang."
        gib_zurück nichts
    setze ziel auf raum["ausgaenge"][richtung]
    setze zraum auf welt["raeume"][ziel]
    wenn zraum["gesperrt"] != nichts:
        zeige "Der Weg ist verschlossen. Du brauchst einen passenden Schluessel."
        gib_zurück nichts
    spieler["ort"] = ziel
    zeige_raum(welt, spieler)

funktion cmd_nimm(welt, spieler, name):
    setze raum auf welt["raeume"][spieler["ort"]]
    wenn raum["dunkel"] und spieler["licht"] == falsch:
        zeige "Im Dunkeln findest du nichts."
        gib_zurück nichts
    setze i auf 0
    setze gefunden auf falsch
    solange i < länge(raum["items"]):
        wenn raum["items"][i] == name:
            setze gefunden auf wahr
        setze i auf i + 1
    wenn gefunden == falsch:
        zeige "Das ist hier nicht."
        gib_zurück nichts
    raum["items"] = entferne_aus_liste(raum["items"], name)
    spieler["inventar"].hinzufügen(name)
    zeige "Du nimmst " + name + "."
    wenn name == "goldbeutel":
        spieler["gold"] = spieler["gold"] + 10
        zeige "(+10 Gold)"

funktion cmd_benutze(welt, spieler, name):
    wenn hat_item(spieler, name) == falsch:
        zeige "Du hast kein " + name + "."
        gib_zurück nichts
    setze item auf welt["items"][name]
    item.benutze(spieler, welt)
    wenn name == "amulett":
        welt["flags"]["amulett_gezeigt"] = wahr
        setze raum auf welt["raeume"][spieler["ort"]]
        setze hat_w auf falsch
        setze i auf 0
        solange i < länge(raum["npcs"]):
            wenn raum["npcs"][i] == "waechter":
                setze hat_w auf wahr
            setze i auf i + 1
        wenn hat_w:
            zeige "Der Waechter verneigt sich und tritt zur Seite. Der Tempel ist nun offen."
            welt["flags"]["waechter_besiegt"] = wahr
            raum["npcs"] = entferne_aus_liste(raum["npcs"], "waechter")
            welt["raeume"]["tempel"]["gesperrt"] = nichts

funktion cmd_schau(welt, spieler):
    zeige_raum(welt, spieler)

funktion cmd_inventar(spieler):
    zeige "=== Inventar ==="
    wenn länge(spieler["inventar"]) == 0:
        zeige "(leer)"
    sonst:
        setze i auf 0
        solange i < länge(spieler["inventar"]):
            zeige "  - " + spieler["inventar"][i]
            setze i auf i + 1

funktion cmd_rede(welt, spieler, name):
    setze raum auf welt["raeume"][spieler["ort"]]
    setze gefunden auf falsch
    setze i auf 0
    solange i < länge(raum["npcs"]):
        wenn raum["npcs"][i] == name:
            setze gefunden auf wahr
        setze i auf i + 1
    wenn gefunden == falsch:
        zeige "Hier ist niemand mit diesem Namen."
        gib_zurück nichts
    setze npc auf welt["npcs"][name]
    npc.rede(spieler, welt)

funktion cmd_tausche_gold(welt, spieler):
    setze raum auf welt["raeume"][spieler["ort"]]
    setze hat_h auf falsch
    setze i auf 0
    solange i < länge(raum["npcs"]):
        wenn raum["npcs"][i] == "haendler":
            setze hat_h auf wahr
        setze i auf i + 1
    wenn hat_h == falsch:
        zeige "Hier gibt es keinen Haendler."
        gib_zurück nichts
    wenn spieler["gold"] < 5:
        zeige "Du hast nicht genug Gold (5 noetig)."
        gib_zurück nichts
    spieler["gold"] = spieler["gold"] - 5
    spieler["inventar"].hinzufügen("heiltrank")
    zeige "Du erhaeltst einen heiltrank. (-5 Gold)"

funktion cmd_greife_an(welt, spieler, name):
    setze raum auf welt["raeume"][spieler["ort"]]
    setze gefunden auf falsch
    setze i auf 0
    solange i < länge(raum["npcs"]):
        wenn raum["npcs"][i] == name:
            setze gefunden auf wahr
        setze i auf i + 1
    wenn gefunden == falsch:
        zeige "Hier ist kein " + name + " zum Angreifen."
        gib_zurück nichts
    setze npc auf welt["npcs"][name]
    wenn npc.hp <= 0:
        zeige npc.name + " ist bereits besiegt."
        gib_zurück nichts
    zeige "--- Kampfrunde ---"
    setze dmg_s auf spieler["schaden_bonus"]
    npc.hp = npc.hp - dmg_s
    zeige "Du triffst " + npc.name + " fuer " + text(dmg_s) + " Schaden. (" + npc.name + " HP: " + text(npc.hp) + ")"
    wenn npc.hp <= 0:
        zeige npc.name + " ist besiegt!"
        raum["npcs"] = entferne_aus_liste(raum["npcs"], name)
        welt["flags"][name + "_besiegt"] = wahr
        gib_zurück nichts
    setze dmg_n auf npc.schaden
    spieler["hp"] = spieler["hp"] - dmg_n
    zeige npc.name + " schlaegt zurueck fuer " + text(dmg_n) + " Schaden. (Deine HP: " + text(spieler["hp"]) + ")"
    wenn spieler["hp"] <= 0:
        zeige ""
        zeige "*** Du bist gestorben. Game Over. ***"
        spieler["ort"] = "tot"

# ------------------------------------------------------------
# Save / Load (JSON)
# ------------------------------------------------------------

funktion cmd_speichern(welt, spieler):
    setze d auf {}
    d["ort"] = spieler["ort"]
    d["hp"] = spieler["hp"]
    d["hp_max"] = spieler["hp_max"]
    d["schaden_bonus"] = spieler["schaden_bonus"]
    d["waffe"] = spieler["waffe"]
    d["inventar"] = spieler["inventar"]
    d["licht"] = spieler["licht"]
    d["gold"] = spieler["gold"]
    setze f auf {}
    setze keys auf welt["flags"].schlüssel()
    setze i auf 0
    solange i < länge(keys):
        f[keys[i]] = welt["flags"][keys[i]]
        setze i auf i + 1
    d["flags"] = f
    setze ri auf {}
    setze rkeys auf welt["raeume"].schlüssel()
    setze j auf 0
    solange j < länge(rkeys):
        ri[rkeys[j]] = welt["raeume"][rkeys[j]]["items"]
        setze j auf j + 1
    d["raum_items"] = ri
    datei_schreiben("adventure_save.json", json_string(d))
    zeige "Spiel gespeichert (adventure_save.json)."

funktion cmd_laden(welt, spieler):
    wenn datei_existiert("adventure_save.json") == falsch:
        zeige "Kein Speicherstand gefunden."
        gib_zurück nichts
    setze s auf datei_lesen("adventure_save.json")
    setze d auf json_parse(s)
    spieler["ort"] = d["ort"]
    spieler["hp"] = d["hp"]
    spieler["hp_max"] = d["hp_max"]
    spieler["schaden_bonus"] = d["schaden_bonus"]
    spieler["waffe"] = d["waffe"]
    spieler["inventar"] = d["inventar"]
    spieler["licht"] = d["licht"]
    spieler["gold"] = d["gold"]
    setze f auf d["flags"]
    setze keys auf f.schlüssel()
    setze i auf 0
    solange i < länge(keys):
        welt["flags"][keys[i]] = f[keys[i]]
        setze i auf i + 1
    setze ri auf d["raum_items"]
    setze rkeys auf ri.schlüssel()
    setze j auf 0
    solange j < länge(rkeys):
        welt["raeume"][rkeys[j]]["items"] = ri[rkeys[j]]
        setze j auf j + 1
    zeige "Spiel geladen."
    zeige_raum(welt, spieler)

# ------------------------------------------------------------
# Hilfe
# ------------------------------------------------------------

funktion zeige_hilfe():
    zeige "=== Befehle ==="
    zeige "  gehe <richtung>  (n/s/o/w/oben/unten/tiefen)"
    zeige "  schau / l"
    zeige "  nimm <item>"
    zeige "  benutze <item> / b <item>"
    zeige "  inventar / i"
    zeige "  rede <name>"
    zeige "  tausche           (mit Haendler)"
    zeige "  greife_an <name>"
    zeige "  status"
    zeige "  speichern / laden"
    zeige "  beenden / q"

# ------------------------------------------------------------
# Parser
# ------------------------------------------------------------

funktion parse_cmd(eingabe_text, welt, spieler):
    setze t auf eingabe_text.klein()
    setze teile auf t.teilen(" ")
    wenn länge(teile) == 0:
        gib_zurück wahr
    setze cmd auf teile[0]
    setze arg auf ""
    wenn länge(teile) > 1:
        setze arg auf teile[1]

    wenn cmd == "gehe" oder cmd == "g":
        cmd_gehen(welt, spieler, arg)
    sonst wenn cmd == "norden" oder cmd == "n":
        cmd_gehen(welt, spieler, "norden")
    sonst wenn cmd == "sueden" oder cmd == "s":
        cmd_gehen(welt, spieler, "sueden")
    sonst wenn cmd == "osten" oder cmd == "o":
        cmd_gehen(welt, spieler, "osten")
    sonst wenn cmd == "westen" oder cmd == "w":
        cmd_gehen(welt, spieler, "westen")
    sonst wenn cmd == "oben":
        cmd_gehen(welt, spieler, "oben")
    sonst wenn cmd == "unten":
        cmd_gehen(welt, spieler, "unten")
    sonst wenn cmd == "tiefen":
        cmd_gehen(welt, spieler, "tiefen")
    sonst wenn cmd == "nimm":
        cmd_nimm(welt, spieler, arg)
    sonst wenn cmd == "benutze" oder cmd == "b":
        cmd_benutze(welt, spieler, arg)
    sonst wenn cmd == "schau" oder cmd == "l":
        cmd_schau(welt, spieler)
    sonst wenn cmd == "inventar" oder cmd == "i":
        cmd_inventar(spieler)
    sonst wenn cmd == "rede":
        cmd_rede(welt, spieler, arg)
    sonst wenn cmd == "tausche":
        cmd_tausche_gold(welt, spieler)
    sonst wenn cmd == "greife_an" oder cmd == "angriff":
        cmd_greife_an(welt, spieler, arg)
    sonst wenn cmd == "status":
        zeige_status(spieler)
    sonst wenn cmd == "speichern":
        cmd_speichern(welt, spieler)
    sonst wenn cmd == "laden":
        cmd_laden(welt, spieler)
    sonst wenn cmd == "hilfe" oder cmd == "h":
        zeige_hilfe()
    sonst wenn cmd == "beenden" oder cmd == "q":
        gib_zurück falsch
    sonst:
        zeige "Unbekannter Befehl: '" + cmd + "'. (hilfe fuer Liste)"
    gib_zurück wahr

# ------------------------------------------------------------
# Hauptschleife
# ------------------------------------------------------------

zeige "============================================"
zeige "  Die verlorene Krone von Aerandur"
zeige "  Ein moo Text-Adventure"
zeige "============================================"
zeige ""
zeige "Tippe 'hilfe' fuer die Befehlsliste."

setze welt auf baue_welt()
setze spieler auf neuer_spieler()
zeige_raum(welt, spieler)

setze aktiv auf wahr
solange aktiv:
    wenn spieler["ort"] == "tot":
        setze aktiv auf falsch
    sonst:
        setze eingabe_text auf eingabe("> ")
        wenn eingabe_text != "":
            setze erg auf parse_cmd(eingabe_text, welt, spieler)
            wenn erg == falsch:
                setze aktiv auf falsch
                zeige "Auf Wiedersehen, Abenteurer!"

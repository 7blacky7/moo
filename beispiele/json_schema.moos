# JSON-Schema-Validator — moo Stresstest
# Nutzt die neuen Compiler-Features:
#   - Vererbung mit Method-Override + Dynamic Dispatch
#   - Globale Variablen in Funktionen
#   - Multi-line Listen/Dict-Literale
#   - try/catch mit return aus Methoden
#
# Architektur:
#   Validator          (Basis-Klasse, rekursiver Dispatch)
#    ├─ StringValidator
#    ├─ ZahlValidator
#    ├─ BoolValidator
#    ├─ NullValidator
#    ├─ ArrayValidator
#    └─ ObjektValidator
#
# Der Hauptdispatcher waehlt per type-Feld die Subklasse und
# ruft deren validiere()-Methode auf — echtes Dynamic Dispatch.


# --- Globale Konfiguration (nutzt Bug-Fix fuer Globals in Funktionen) ---

setze TYP_MOO auf {
    "string": "Text",
    "number": "Zahl",
    "integer": "Zahl",
    "boolean": "Wahrheitswert",
    "null": "Nichts",
    "array": "Liste",
    "object": "Woerterbuch"
}

setze GESAMT_TESTS auf 0
setze GESAMT_OK auf 0


# --- Basis-Klasse ---

klasse Validator:
    funktion erstelle():
        selbst.fehler = []

    funktion fuege_fehler(pfad, nachricht):
        wenn pfad == "":
            selbst.fehler.hinzufügen("/: " + nachricht)
        sonst:
            selbst.fehler.hinzufügen(pfad + ": " + nachricht)

    # Typ-Check (wird von allen Subklassen gerufen)
    funktion typ_passt(wert, schema):
        wenn schema.hat("type") == falsch:
            gib_zurück wahr
        setze erwartet auf schema["type"]
        setze tatsaechlich auf typ_von(wert)
        wenn TYP_MOO.hat(erwartet) == falsch:
            gib_zurück wahr
        setze erwartet_moo auf TYP_MOO[erwartet]
        gib_zurück tatsaechlich == erwartet_moo

    # Enum-Check (generisch fuer alle Typen)
    funktion enum_check(wert, schema, pfad):
        wenn schema.hat("enum"):
            setze gefunden auf falsch
            für kandidat in schema["enum"]:
                wenn kandidat == wert:
                    setze gefunden auf wahr
            wenn gefunden == falsch:
                selbst.fuege_fehler(pfad, "nicht im enum")

    # Wird von jeder Subklasse ueberschrieben
    funktion validiere(wert, schema, pfad):
        selbst.enum_check(wert, schema, pfad)

    # Haupteinstieg — Dispatch auf richtige Subklasse
    funktion pruefe(wert, schema):
        selbst.fehler = []
        selbst.dispatch(wert, schema, "")
        gib_zurück selbst.fehler

    funktion dispatch(wert, schema, pfad):
        wenn selbst.typ_passt(wert, schema) == falsch:
            setze erwartet auf schema["type"]
            setze tatsaechlich auf typ_von(wert)
            selbst.fuege_fehler(pfad, "erwartet " + erwartet + ", aber " + tatsaechlich)
            gib_zurück nichts

        wenn schema.hat("type") == falsch:
            selbst.enum_check(wert, schema, pfad)
            gib_zurück nichts

        setze t auf schema["type"]
        setze sub auf selbst.fuer_typ(t)
        sub.fehler = selbst.fehler
        sub.validiere(wert, schema, pfad)
        selbst.fehler = sub.fehler

    # Factory — gibt passenden Sub-Validator zurueck.
    # Dank Dynamic Dispatch sieht Validator.dispatch() die richtige validiere().
    funktion fuer_typ(t):
        wenn t == "string":
            gib_zurück neu StringValidator()
        wenn t == "number":
            gib_zurück neu ZahlValidator()
        wenn t == "integer":
            gib_zurück neu ZahlValidator()
        wenn t == "boolean":
            gib_zurück neu BoolValidator()
        wenn t == "null":
            gib_zurück neu NullValidator()
        wenn t == "array":
            gib_zurück neu ArrayValidator()
        wenn t == "object":
            gib_zurück neu ObjektValidator()
        gib_zurück neu Validator()


# --- String ---

klasse StringValidator(Validator):
    funktion validiere(wert, schema, pfad):
        selbst.enum_check(wert, schema, pfad)
        setze n auf länge(wert)
        wenn schema.hat("minLength"):
            wenn n < schema["minLength"]:
                selbst.fuege_fehler(pfad, "zu kurz (" + text(n) + " < " + text(schema["minLength"]) + ")")
        wenn schema.hat("maxLength"):
            wenn n > schema["maxLength"]:
                selbst.fuege_fehler(pfad, "zu lang (" + text(n) + " > " + text(schema["maxLength"]) + ")")
        wenn schema.hat("pattern"):
            setze rx auf regex(schema["pattern"])
            wenn passt(wert, rx) == falsch:
                selbst.fuege_fehler(pfad, "passt nicht auf Muster " + schema["pattern"])


# --- Zahl ---

klasse ZahlValidator(Validator):
    funktion validiere(wert, schema, pfad):
        selbst.enum_check(wert, schema, pfad)
        wenn schema.hat("minimum"):
            wenn wert < schema["minimum"]:
                selbst.fuege_fehler(pfad, "zu klein (" + text(wert) + " < " + text(schema["minimum"]) + ")")
        wenn schema.hat("maximum"):
            wenn wert > schema["maximum"]:
                selbst.fuege_fehler(pfad, "zu gross (" + text(wert) + " > " + text(schema["maximum"]) + ")")


# --- Bool ---

klasse BoolValidator(Validator):
    funktion validiere(wert, schema, pfad):
        selbst.enum_check(wert, schema, pfad)


# --- Null ---

klasse NullValidator(Validator):
    funktion validiere(wert, schema, pfad):
        selbst.enum_check(wert, schema, pfad)


# --- Array ---

klasse ArrayValidator(Validator):
    funktion validiere(wert, schema, pfad):
        selbst.enum_check(wert, schema, pfad)
        setze n auf länge(wert)
        wenn schema.hat("minItems"):
            wenn n < schema["minItems"]:
                selbst.fuege_fehler(pfad, "zu wenige Elemente (" + text(n) + " < " + text(schema["minItems"]) + ")")
        wenn schema.hat("maxItems"):
            wenn n > schema["maxItems"]:
                selbst.fuege_fehler(pfad, "zu viele Elemente (" + text(n) + " > " + text(schema["maxItems"]) + ")")
        wenn schema.hat("items"):
            setze item_schema auf schema["items"]
            setze i auf 0
            setze root auf neu Validator()
            root.fehler = selbst.fehler
            solange i < n:
                root.dispatch(wert[i], item_schema, pfad + "/" + text(i))
                setze i auf i + 1
            selbst.fehler = root.fehler


# --- Objekt ---

klasse ObjektValidator(Validator):
    funktion validiere(wert, schema, pfad):
        selbst.enum_check(wert, schema, pfad)

        wenn schema.hat("required"):
            für schluessel in schema["required"]:
                wenn wert.hat(schluessel) == falsch:
                    selbst.fuege_fehler(pfad + "/" + schluessel, "Pflichtfeld fehlt")

        setze props auf {}
        wenn schema.hat("properties"):
            setze props auf schema["properties"]

        setze root auf neu Validator()
        root.fehler = selbst.fehler

        für k in wert.schlüssel():
            wenn props.hat(k):
                root.dispatch(wert[k], props[k], pfad + "/" + k)
            sonst:
                wenn schema.hat("additionalProperties"):
                    wenn schema["additionalProperties"] == falsch:
                        root.fuege_fehler(pfad + "/" + k, "zusaetzliches Feld nicht erlaubt")

        selbst.fehler = root.fehler


# --- Test-Harness (nutzt globale Vars in Funktionen!) ---

funktion test_ok(name, fehler, erwartet):
    setze GESAMT_TESTS auf GESAMT_TESTS + 1
    wenn länge(fehler) == erwartet:
        zeige "  OK  " + name
        setze GESAMT_OK auf GESAMT_OK + 1
    sonst:
        zeige "  FAIL " + name + " — erwartet " + text(erwartet) + " Fehler, bekam " + text(länge(fehler))
        für f in fehler:
            zeige "       -> " + f


# --- Haupttests ---

setze v auf neu Validator()

zeige "=== JSON-Schema-Validator (Vererbung + Dynamic Dispatch) ==="
zeige ""

# 1. String Basis
zeige "Schema 1: String (3-10 Zeichen)"
setze s1 auf {"type": "string", "minLength": 3, "maxLength": 10}
test_ok("hallo ok", v.pruefe("hallo", s1), 0)
test_ok("hi zu kurz", v.pruefe("hi", s1), 1)
test_ok("zu langer string", v.pruefe("abcdefghijk", s1), 1)
test_ok("kein string", v.pruefe(42, s1), 1)

# 2. Zahl mit Bereich
zeige ""
zeige "Schema 2: Zahl 0-100"
setze s2 auf {"type": "number", "minimum": 0, "maximum": 100}
test_ok("50 ok", v.pruefe(50, s2), 0)
test_ok("-1 zu klein", v.pruefe(-1, s2), 1)
test_ok("200 zu gross", v.pruefe(200, s2), 1)

# 3. Enum
zeige ""
zeige "Schema 3: Enum Ampelfarben"
setze s3 auf {"type": "string", "enum": ["rot", "gelb", "gruen"]}
test_ok("rot ok", v.pruefe("rot", s3), 0)
test_ok("blau abgelehnt", v.pruefe("blau", s3), 1)

# 4. Objekt mit required
zeige ""
zeige "Schema 4: User-Objekt"
setze s4 auf {
    "type": "object",
    "required": ["name", "age"],
    "properties": {
        "name": {"type": "string", "minLength": 1},
        "age": {"type": "number", "minimum": 0, "maximum": 150}
    }
}
test_ok("Anna 30 ok", v.pruefe({"name": "Anna", "age": 30}, s4), 0)
test_ok("age fehlt", v.pruefe({"name": "Bob"}, s4), 1)
test_ok("age negativ", v.pruefe({"name": "Eve", "age": -5}, s4), 1)
test_ok("name leer + age zu gross", v.pruefe({"name": "", "age": 999}, s4), 2)

# 5. Array mit items
zeige ""
zeige "Schema 5: Zahlen-Array"
setze s5 auf {"type": "array", "minItems": 1, "maxItems": 5, "items": {"type": "number"}}
test_ok("[1,2,3] ok", v.pruefe([1, 2, 3], s5), 0)
test_ok("[] zu kurz", v.pruefe([], s5), 1)
test_ok("[1,2,3,4,5,6] zu lang", v.pruefe([1, 2, 3, 4, 5, 6], s5), 1)
test_ok("[1,'x',3] typ-fehler", v.pruefe([1, "x", 3], s5), 1)

# 6. Regex Email
zeige ""
zeige "Schema 6: Email-Regex"
setze s6 auf {"type": "string", "pattern": "^[a-z]+@[a-z]+\\.[a-z]+$"}
test_ok("test@example.com", v.pruefe("test@example.com", s6), 0)
test_ok("keine-email", v.pruefe("keine-email", s6), 1)

# 7. additionalProperties false
zeige ""
zeige "Schema 7: strict Objekt"
setze s7 auf {
    "type": "object",
    "properties": {"a": {"type": "number"}},
    "additionalProperties": falsch
}
test_ok("nur a ok", v.pruefe({"a": 1}, s7), 0)
test_ok("extra b abgelehnt", v.pruefe({"a": 1, "b": 2}, s7), 1)

# 8. Verschachtelt
zeige ""
zeige "Schema 8: verschachteltes Objekt"
setze s8 auf {
    "type": "object",
    "properties": {
        "user": {
            "type": "object",
            "required": ["name"],
            "properties": {
                "name": {"type": "string"}
            }
        }
    }
}
test_ok("{user:{name:Anna}}", v.pruefe({"user": {"name": "Anna"}}, s8), 0)
test_ok("{user:{}}", v.pruefe({"user": {}}, s8), 1)

# 9. Array von Produkten
zeige ""
zeige "Schema 9: Produkt-Katalog"
setze produkt auf {
    "type": "object",
    "required": ["name", "preis"],
    "properties": {
        "name": {"type": "string", "minLength": 1},
        "preis": {"type": "number", "minimum": 0}
    }
}
setze s9 auf {"type": "array", "items": produkt}
setze gueltig auf [{"name": "Apfel", "preis": 1.5}, {"name": "Brot", "preis": 3.0}]
test_ok("2 gueltige Produkte", v.pruefe(gueltig, s9), 0)
setze kaputt auf [{"name": "Apfel", "preis": 1.5}, {"name": "", "preis": -5}]
test_ok("2 Fehler im zweiten", v.pruefe(kaputt, s9), 2)

# 10. Integer + Pattern kombiniert (komplex)
zeige ""
zeige "Schema 10: Bestellung komplex"
setze s10 auf {
    "type": "object",
    "required": ["id", "status", "items", "total"],
    "properties": {
        "id": {"type": "string", "pattern": "^BE[0-9]+$"},
        "status": {"type": "string", "enum": ["offen", "bezahlt", "versandt"]},
        "items": {
            "type": "array",
            "minItems": 1,
            "items": {
                "type": "object",
                "required": ["name", "menge"],
                "properties": {
                    "name": {"type": "string"},
                    "menge": {"type": "integer", "minimum": 1}
                }
            }
        },
        "total": {"type": "number", "minimum": 0}
    }
}

setze bestellung_ok auf {
    "id": "BE12345",
    "status": "bezahlt",
    "items": [{"name": "Apfel", "menge": 3}, {"name": "Brot", "menge": 1}],
    "total": 5.50
}
test_ok("gueltige Bestellung", v.pruefe(bestellung_ok, s10), 0)

setze bestellung_kaputt auf {
    "id": "FALSCH",
    "status": "unbekannt",
    "items": [],
    "total": -1
}
test_ok("4 Fehler in kaputter Bestellung", v.pruefe(bestellung_kaputt, s10), 4)

zeige ""
zeige "=========================================="
zeige "Ergebnis: " + text(GESAMT_OK) + "/" + text(GESAMT_TESTS) + " Tests gruen"
zeige "=========================================="

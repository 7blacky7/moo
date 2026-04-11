# JSON-Schema-Validator — moo Stresstest
# Rekursive Validierung von JSON gegen ein Schema.
# Unterstuetzt: type, required, properties, additionalProperties,
#   items, minItems, maxItems, minLength, maxLength, pattern,
#   minimum, maximum, enum
# Fehler mit JSON-Pointer Pfad: /user/age

klasse SchemaValidator:
    funktion erstelle():
        selbst.fehler = []

    funktion fuege_fehler(pfad, nachricht):
        wenn pfad == "":
            selbst.fehler.hinzufügen("/: " + nachricht)
        sonst:
            selbst.fehler.hinzufügen(pfad + ": " + nachricht)

    funktion map_typ(schema_typ):
        wenn schema_typ == "string":
            gib_zurück "Text"
        wenn schema_typ == "number":
            gib_zurück "Zahl"
        wenn schema_typ == "integer":
            gib_zurück "Zahl"
        wenn schema_typ == "boolean":
            gib_zurück "Wahrheitswert"
        wenn schema_typ == "null":
            gib_zurück "Nichts"
        wenn schema_typ == "array":
            gib_zurück "Liste"
        wenn schema_typ == "object":
            gib_zurück "Woerterbuch"
        gib_zurück "?"

    funktion validiere(wert, schema, pfad):
        wenn schema.hat("type"):
            setze erwartet auf schema["type"]
            setze tatsaechlich auf typ_von(wert)
            setze erwartet_moo auf selbst.map_typ(erwartet)
            wenn tatsaechlich != erwartet_moo:
                selbst.fuege_fehler(pfad, "erwartet " + erwartet + ", aber " + tatsaechlich)
                gib_zurück nichts

        wenn schema.hat("enum"):
            setze gefunden auf falsch
            für kandidat in schema["enum"]:
                wenn kandidat == wert:
                    setze gefunden auf wahr
            wenn gefunden == falsch:
                selbst.fuege_fehler(pfad, "nicht im enum")

        wenn schema.hat("type"):
            setze t auf schema["type"]
            wenn t == "object":
                selbst.validiere_objekt(wert, schema, pfad)
            wenn t == "array":
                selbst.validiere_array(wert, schema, pfad)
            wenn t == "string":
                selbst.validiere_string(wert, schema, pfad)
            wenn t == "number":
                selbst.validiere_zahl(wert, schema, pfad)
            wenn t == "integer":
                selbst.validiere_zahl(wert, schema, pfad)

    funktion validiere_objekt(wert, schema, pfad):
        wenn schema.hat("required"):
            für schluessel in schema["required"]:
                wenn wert.hat(schluessel) == falsch:
                    selbst.fuege_fehler(pfad + "/" + schluessel, "Pflichtfeld fehlt")

        setze props auf {}
        wenn schema.hat("properties"):
            setze props auf schema["properties"]

        für k in wert.schlüssel():
            wenn props.hat(k):
                selbst.validiere(wert[k], props[k], pfad + "/" + k)
            sonst:
                wenn schema.hat("additionalProperties"):
                    wenn schema["additionalProperties"] == falsch:
                        selbst.fuege_fehler(pfad + "/" + k, "zusaetzliches Feld nicht erlaubt")

    funktion validiere_array(wert, schema, pfad):
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
            solange i < n:
                selbst.validiere(wert[i], item_schema, pfad + "/" + text(i))
                setze i auf i + 1

    funktion validiere_string(wert, schema, pfad):
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

    funktion validiere_zahl(wert, schema, pfad):
        wenn schema.hat("minimum"):
            wenn wert < schema["minimum"]:
                selbst.fuege_fehler(pfad, "zu klein (" + text(wert) + " < " + text(schema["minimum"]) + ")")
        wenn schema.hat("maximum"):
            wenn wert > schema["maximum"]:
                selbst.fuege_fehler(pfad, "zu gross (" + text(wert) + " > " + text(schema["maximum"]) + ")")

    funktion pruefe(wert, schema):
        selbst.fehler = []
        selbst.validiere(wert, schema, "")
        gib_zurück selbst.fehler


# ============================================================
# TESTS
# ============================================================

setze tests_ok auf 0
setze tests_fail auf 0

setze v auf neu SchemaValidator()

zeige "=== JSON-Schema-Validator Tests ==="
zeige ""

# Test 1: Einfacher String
zeige "Test 1: String-Validierung"
setze schema1 auf {"type": "string", "minLength": 3, "maxLength": 10}
setze fehler auf v.pruefe("hallo", schema1)
wenn länge(fehler) == 0:
    zeige "  OK: gueltig"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe("hi", schema1)
wenn länge(fehler) == 1:
    zeige "  OK: zu kurz erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL: " + text(länge(fehler)) + " Fehler"
    setze tests_fail auf tests_fail + 1

# Test 2: Zahl mit Bereich
zeige ""
zeige "Test 2: Zahl mit Bereich"
setze schema2 auf {"type": "number", "minimum": 0, "maximum": 100}
setze fehler auf v.pruefe(50, schema2)
wenn länge(fehler) == 0:
    zeige "  OK: 50 gueltig"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe(150, schema2)
wenn länge(fehler) == 1:
    zeige "  OK: 150 zu gross"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

# Test 3: Enum
zeige ""
zeige "Test 3: Enum"
setze schema3 auf {"type": "string", "enum": ["rot", "gruen", "blau"]}
setze fehler auf v.pruefe("rot", schema3)
wenn länge(fehler) == 0:
    zeige "  OK: rot gueltig"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe("gelb", schema3)
wenn länge(fehler) == 1:
    zeige "  OK: gelb abgelehnt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

# Test 4: Objekt mit required
zeige ""
zeige "Test 4: Objekt mit required-Feldern"
setze user_schema auf {"type": "object", "required": ["name", "age"], "properties": {"name": {"type": "string"}, "age": {"type": "number", "minimum": 0}}}
setze user1 auf {"name": "Anna", "age": 30}
setze fehler auf v.pruefe(user1, user_schema)
wenn länge(fehler) == 0:
    zeige "  OK: gueltiger User"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

setze user2 auf {"name": "Bob"}
setze fehler auf v.pruefe(user2, user_schema)
wenn länge(fehler) == 1:
    zeige "  OK: fehlendes age erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL: " + text(länge(fehler))
    setze tests_fail auf tests_fail + 1

setze user3 auf {"name": "Eve", "age": -5}
setze fehler auf v.pruefe(user3, user_schema)
wenn länge(fehler) == 1:
    zeige "  OK: negatives age erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL: " + text(länge(fehler))
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

# Test 5: Array mit items
zeige ""
zeige "Test 5: Array mit items"
setze arr_schema auf {"type": "array", "minItems": 1, "maxItems": 5, "items": {"type": "number"}}
setze fehler auf v.pruefe([1, 2, 3], arr_schema)
wenn länge(fehler) == 0:
    zeige "  OK: [1,2,3] gueltig"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe([], arr_schema)
wenn länge(fehler) == 1:
    zeige "  OK: leeres Array abgelehnt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe([1, "x", 3], arr_schema)
wenn länge(fehler) == 1:
    zeige "  OK: String in Zahlen-Array erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL: " + text(länge(fehler))
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

# Test 6: Regex-Pattern (Email)
zeige ""
zeige "Test 6: Regex-Pattern"
setze email_schema auf {"type": "string", "pattern": "^[a-z]+@[a-z]+\\.[a-z]+$"}
setze fehler auf v.pruefe("test@example.com", email_schema)
wenn länge(fehler) == 0:
    zeige "  OK: gueltige Email"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe("keine-email", email_schema)
wenn länge(fehler) == 1:
    zeige "  OK: ungueltige Email erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

# Test 7: Additional Properties verboten
zeige ""
zeige "Test 7: additionalProperties: false"
setze strict_schema auf {"type": "object", "properties": {"a": {"type": "number"}}, "additionalProperties": falsch}
setze fehler auf v.pruefe({"a": 1}, strict_schema)
wenn länge(fehler) == 0:
    zeige "  OK: nur a"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe({"a": 1, "b": 2}, strict_schema)
wenn länge(fehler) == 1:
    zeige "  OK: extra b erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

# Test 8: Verschachteltes Objekt
zeige ""
zeige "Test 8: Verschachteltes Objekt"
setze nested auf {"type": "object", "properties": {"user": {"type": "object", "required": ["name"], "properties": {"name": {"type": "string"}}}}}
setze fehler auf v.pruefe({"user": {"name": "Anna"}}, nested)
wenn länge(fehler) == 0:
    zeige "  OK: gueltig verschachtelt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

setze fehler auf v.pruefe({"user": {}}, nested)
wenn länge(fehler) == 1:
    zeige "  OK: verschachteltes required-fehlt erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL: " + text(länge(fehler))
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

# Test 9: Array von Objekten
zeige ""
zeige "Test 9: Array von Objekten (Produkte)"
setze produkt auf {"type": "object", "required": ["name", "preis"], "properties": {"name": {"type": "string", "minLength": 1}, "preis": {"type": "number", "minimum": 0}}}
setze katalog_schema auf {"type": "array", "items": produkt}
setze katalog auf [{"name": "Apfel", "preis": 1.5}, {"name": "Brot", "preis": 3.0}]
setze fehler auf v.pruefe(katalog, katalog_schema)
wenn länge(fehler) == 0:
    zeige "  OK: Katalog gueltig"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

setze kaputt auf [{"name": "Apfel", "preis": 1.5}, {"name": "", "preis": -5}]
setze fehler auf v.pruefe(kaputt, katalog_schema)
wenn länge(fehler) == 2:
    zeige "  OK: 2 Fehler in kaputtem Katalog"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL: " + text(länge(fehler)) + " Fehler"
    für f in fehler:
        zeige "    -> " + f
    setze tests_fail auf tests_fail + 1

# Test 10: Komplett falscher Typ
zeige ""
zeige "Test 10: Typ-Mismatch"
setze fehler auf v.pruefe(42, {"type": "string"})
wenn länge(fehler) == 1:
    zeige "  OK: Zahl statt String erkannt"
    setze tests_ok auf tests_ok + 1
sonst:
    zeige "  FAIL"
    setze tests_fail auf tests_fail + 1

# Ergebnis
zeige ""
zeige "=========================================="
zeige "Ergebnis: " + text(tests_ok) + " OK, " + text(tests_fail) + " FAIL"
zeige "=========================================="

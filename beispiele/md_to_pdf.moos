# ============================================================
# moo Markdown → PDF Converter
#
# Kompilieren: moo-compiler compile md_to_pdf.moo -o md_to_pdf
# Starten:     ./md_to_pdf
#
# Liest ein Markdown-Subset (Headings, Listen, Absaetze, Inline
# code/bold/italic) und schreibt ein gueltiges PDF 1.4 mit
# mehreren Seiten, xref-Tabelle und MediaBox 612x792 (US Letter).
# ============================================================

# Seite: 612 x 792 (Letter 72 dpi)
konstante PAGE_W auf 612
konstante PAGE_H auf 792
konstante MARGIN_L auf 50
konstante MARGIN_T auf 742
konstante MARGIN_B auf 60
konstante SIZE_H1 auf 24
konstante SIZE_H2 auf 18
konstante SIZE_H3 auf 14
konstante SIZE_BODY auf 12
konstante SIZE_CODE auf 11
konstante LINE_GAP auf 4

# Max. Zeichen pro Zeile (Proxy-Wrap; Helvetica-Metriken
# sind nicht exakt, aber fuer Monospace-Approximation reicht's)
konstante MAX_COL auf 72

# --- Helper ---
funktion push_ascii(liste, s):
    setze bs auf bytes_zu_liste(s)
    setze i auf 0
    solange i < bs.länge():
        liste.hinzufügen(bs[i])
        setze i auf i + 1

funktion zehn_stellen(n):
    setze s auf text(n)
    solange länge(s) < 10:
        setze s auf "0" + s
    gib_zurück s

# Escape fuer PDF-Strings: ( ) und \ verdoppeln
funktion pdf_escape(s):
    setze out auf ""
    setze i auf 0
    solange i < länge(s):
        setze c auf s[i]
        wenn c == "(":
            setze out auf out + "\\("
        sonst:
            wenn c == ")":
                setze out auf out + "\\)"
            sonst:
                wenn c == "\\":
                    setze out auf out + "\\\\"
                sonst:
                    setze out auf out + c
        setze i auf i + 1
    gib_zurück out

# Markdown-Inline: **bold** *italic* `code` → Rohtext ohne Marker
funktion strip_inline(s):
    setze out auf ""
    setze i auf 0
    setze n auf länge(s)
    solange i < n:
        setze c auf s[i]
        wenn c == "*":
            # Doppeltes ** auch schlucken
            wenn i + 1 < n und s[i + 1] == "*":
                setze i auf i + 1
            setze i auf i + 1
        sonst:
            wenn c == "`":
                setze i auf i + 1
            sonst:
                setze out auf out + c
                setze i auf i + 1
    gib_zurück out

# Zeile auf max_col Zeichen wrappen, gibt Liste von Teil-Zeilen
funktion wrap_line(s, max_col):
    setze teile auf []
    setze n auf länge(s)
    wenn n == 0:
        teile.hinzufügen("")
        gib_zurück teile
    setze start auf 0
    solange start < n:
        setze ende auf start + max_col
        wenn ende >= n:
            setze ende auf n
            teile.hinzufügen(s.slice(start, ende))
            setze start auf n
        sonst:
            # Back-Scan auf letztes space
            setze brk auf ende
            solange brk > start und s[brk] != " ":
                setze brk auf brk - 1
            wenn brk == start:
                setze brk auf ende
            teile.hinzufügen(s.slice(start, brk))
            setze start auf brk
            solange start < n und s[start] == " ":
                setze start auf start + 1
    gib_zurück teile

# --- Markdown Parser: produziert Liste von "Blocks" ---
# Block = {"typ": "h1|h2|h3|body|list|code", "text": ...}
funktion parse_md(src):
    setze bloecke auf []
    # Zeilen-Split manuell
    setze zeilen auf []
    setze buf auf ""
    setze i auf 0
    solange i < länge(src):
        setze c auf src[i]
        wenn c == "\n":
            zeilen.hinzufügen(buf)
            setze buf auf ""
        sonst:
            setze buf auf buf + c
        setze i auf i + 1
    wenn länge(buf) > 0:
        zeilen.hinzufügen(buf)

    setze i auf 0
    solange i < zeilen.länge():
        setze z auf zeilen[i]
        wenn länge(z) == 0:
            # Leerer Absatz → Vertikaler Abstand
            setze b auf {}
            b["typ"] = "leer"
            b["text"] = ""
            bloecke.hinzufügen(b)
        sonst:
            wenn länge(z) >= 4 und z.slice(0, 4) == "### ":
                setze b auf {}
                b["typ"] = "h3"
                b["text"] = strip_inline(z.slice(4, länge(z)))
                bloecke.hinzufügen(b)
            sonst:
                wenn länge(z) >= 3 und z.slice(0, 3) == "## ":
                    setze b auf {}
                    b["typ"] = "h2"
                    b["text"] = strip_inline(z.slice(3, länge(z)))
                    bloecke.hinzufügen(b)
                sonst:
                    wenn länge(z) >= 2 und z.slice(0, 2) == "# ":
                        setze b auf {}
                        b["typ"] = "h1"
                        b["text"] = strip_inline(z.slice(2, länge(z)))
                        bloecke.hinzufügen(b)
                    sonst:
                        wenn länge(z) >= 2 und z.slice(0, 2) == "- ":
                            setze b auf {}
                            b["typ"] = "list"
                            b["text"] = strip_inline(z.slice(2, länge(z)))
                            bloecke.hinzufügen(b)
                        sonst:
                            wenn länge(z) >= 4 und z.slice(0, 4) == "    ":
                                setze b auf {}
                                b["typ"] = "code"
                                b["text"] = z.slice(4, länge(z))
                                bloecke.hinzufügen(b)
                            sonst:
                                setze b auf {}
                                b["typ"] = "body"
                                b["text"] = strip_inline(z)
                                bloecke.hinzufügen(b)
        setze i auf i + 1
    gib_zurück bloecke

# --- Layout: verteile Bloecke auf Seiten ---
# Jede Seite = Liste von "ops", ops haben {size, text, indent, dy}
funktion layout(bloecke):
    setze seiten auf []
    setze aktuell auf []
    setze y auf MARGIN_T

    setze i auf 0
    solange i < bloecke.länge():
        setze b auf bloecke[i]
        setze typ auf b["typ"]

        setze size auf SIZE_BODY
        setze indent auf 0
        setze vorher auf 0
        setze nachher auf 4

        wenn typ == "h1":
            setze size auf SIZE_H1
            setze vorher auf 16
            setze nachher auf 10
        wenn typ == "h2":
            setze size auf SIZE_H2
            setze vorher auf 14
            setze nachher auf 8
        wenn typ == "h3":
            setze size auf SIZE_H3
            setze vorher auf 10
            setze nachher auf 6
        wenn typ == "list":
            setze indent auf 15
        wenn typ == "code":
            setze size auf SIZE_CODE
        wenn typ == "leer":
            setze y auf y - 8
            setze i auf i + 1
        sonst:
            setze text_str auf b["text"]
            wenn typ == "list":
                setze text_str auf "•  " + text_str
            setze wraps auf wrap_line(text_str, MAX_COL)

            setze y auf y - vorher
            setze j auf 0
            solange j < wraps.länge():
                wenn y < MARGIN_B:
                    seiten.hinzufügen(aktuell)
                    setze aktuell auf []
                    setze y auf MARGIN_T
                setze op auf {}
                op["size"] = size
                op["text"] = wraps[j]
                op["x"] = MARGIN_L + indent
                op["y"] = y
                op["font"] = "F1"
                wenn typ == "code":
                    op["font"] = "F2"
                aktuell.hinzufügen(op)
                setze y auf y - (size + LINE_GAP)
                setze j auf j + 1
            setze y auf y - nachher
            setze i auf i + 1

    wenn aktuell.länge() > 0:
        seiten.hinzufügen(aktuell)
    gib_zurück seiten

# --- PDF Content-Stream pro Seite bauen ---
funktion build_content_stream(ops):
    setze s auf "BT\n"
    setze last_font auf ""
    setze last_size auf 0
    setze i auf 0
    solange i < ops.länge():
        setze o auf ops[i]
        wenn o["font"] != last_font oder o["size"] != last_size:
            setze s auf s + "/" + o["font"] + " " + text(o["size"]) + " Tf\n"
            setze last_font auf o["font"]
            setze last_size auf o["size"]
        setze s auf s + "1 0 0 1 " + text(o["x"]) + " " + text(o["y"]) + " Tm\n"
        setze s auf s + "(" + pdf_escape(o["text"]) + ") Tj\n"
        setze i auf i + 1
    setze s auf s + "ET\n"
    gib_zurück s

# --- PDF Writer ---
funktion build_pdf(seiten):
    setze out auf []
    setze offsets auf []
    # Dummy 0 — Object 0 ist nie genutzt
    offsets.hinzufügen(0)

    push_ascii(out, "%PDF-1.4\n")
    # Binary marker (4 non-ASCII bytes)
    out.hinzufügen(0x25)
    out.hinzufügen(0xE2)
    out.hinzufügen(0xE3)
    out.hinzufügen(0xCF)
    out.hinzufügen(0xD3)
    out.hinzufügen(0x0A)

    setze n_seiten auf seiten.länge()
    # Objekt-Struktur:
    #   1 Catalog
    #   2 Pages
    #   3..(3+n_seiten-1)           Page objects
    #   (3+n_seiten)..               Content streams
    #   (3+2*n_seiten)               Font F1 (Helvetica)
    #   (3+2*n_seiten)+1             Font F2 (Courier)

    setze page_obj_start auf 3
    setze content_obj_start auf 3 + n_seiten
    setze font_f1 auf 3 + 2 * n_seiten
    setze font_f2 auf font_f1 + 1

    # Obj 1: Catalog
    offsets.hinzufügen(out.länge())
    push_ascii(out, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n")

    # Obj 2: Pages
    offsets.hinzufügen(out.länge())
    push_ascii(out, "2 0 obj\n<< /Type /Pages /Count " + text(n_seiten) + " /Kids [")
    setze p auf 0
    solange p < n_seiten:
        wenn p > 0:
            push_ascii(out, " ")
        push_ascii(out, text(page_obj_start + p) + " 0 R")
        setze p auf p + 1
    push_ascii(out, "] >>\nendobj\n")

    # Page objects
    setze p auf 0
    solange p < n_seiten:
        offsets.hinzufügen(out.länge())
        setze oid auf page_obj_start + p
        setze cid auf content_obj_start + p
        push_ascii(out, text(oid) + " 0 obj\n")
        push_ascii(out, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + text(PAGE_W) + " " + text(PAGE_H) + "]")
        push_ascii(out, " /Contents " + text(cid) + " 0 R /Resources << /Font << /F1 " + text(font_f1) + " 0 R /F2 " + text(font_f2) + " 0 R >> >> >>\n")
        push_ascii(out, "endobj\n")
        setze p auf p + 1

    # Content streams
    setze p auf 0
    solange p < n_seiten:
        offsets.hinzufügen(out.länge())
        setze cid auf content_obj_start + p
        setze stream auf build_content_stream(seiten[p])
        push_ascii(out, text(cid) + " 0 obj\n")
        push_ascii(out, "<< /Length " + text(länge(stream)) + " >>\nstream\n")
        push_ascii(out, stream)
        push_ascii(out, "endstream\nendobj\n")
        setze p auf p + 1

    # Font objects
    offsets.hinzufügen(out.länge())
    push_ascii(out, text(font_f1) + " 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n")
    offsets.hinzufügen(out.länge())
    push_ascii(out, text(font_f2) + " 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>\nendobj\n")

    # xref
    setze xref_off auf out.länge()
    setze anzahl_obj auf offsets.länge()  # inkl. obj 0
    push_ascii(out, "xref\n0 " + text(anzahl_obj) + "\n")
    push_ascii(out, "0000000000 65535 f \n")
    setze k auf 1
    solange k < anzahl_obj:
        push_ascii(out, zehn_stellen(offsets[k]) + " 00000 n \n")
        setze k auf k + 1

    # trailer + startxref + %%EOF
    push_ascii(out, "trailer\n<< /Size " + text(anzahl_obj) + " /Root 1 0 R >>\n")
    push_ascii(out, "startxref\n" + text(xref_off) + "\n%%EOF\n")

    gib_zurück out

# --- Test-Markdown ---
setze md auf "# moo Markdown → PDF\n\nDas hier ist ein kleiner Test-Konvertierungsflow.\n\n## Features\n\nUnterstuetzte Markdown-Elemente:\n\n- Ueberschriften (H1, H2, H3)\n- Absaetze\n- Listen mit Bullets\n- Inline **bold**, *italic* und `code`\n- Code-Bloecke via Einrueckung\n\n## Beispiel-Code\n\n    funktion hallo():\n        zeige \"Hallo moo!\"\n\n### Details\n\nPDF 1.4 mit Helvetica fuer Body und Courier fuer Code-Bloecke.\nSeitengroesse ist US Letter (612 x 792 pt).\n\n## Noch mehr Text\n\nDies ist ein langer Absatz, der demonstrieren soll wie der Zeilen-Wrap funktioniert, wenn eine Zeile die max_col Grenze ueberschreitet und in mehrere Zeilen umgebrochen werden muss. Der Umbruch passiert am letzten Whitespace vor der Grenze, damit Woerter nicht zerrissen werden.\n\n- Erstes Listen-Element\n- Zweites Listen-Element mit etwas mehr Text zum Testen des Wraps\n- Drittes Listen-Element\n\n### Fazit\n\nPDF-Generierung in pure moo: machbar.\n"

zeige "=== moo Markdown → PDF ==="
zeige "Markdown-Laenge: " + text(länge(md)) + " bytes"

setze bloecke auf parse_md(md)
zeige "Bloecke: " + text(bloecke.länge())

setze seiten auf layout(bloecke)
zeige "Seiten: " + text(seiten.länge())

setze pdf auf build_pdf(seiten)
zeige "PDF-Bytes: " + text(pdf.länge())

datei_schreiben_bytes("/tmp/out.pdf", pdf)
zeige "/tmp/out.pdf geschrieben"

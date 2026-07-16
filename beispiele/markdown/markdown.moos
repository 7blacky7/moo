# Markdown Parser in moo
# Input: Markdown-Text, Output: HTML

# === HTML-Escape ===
funktion html_escape(text):
    setze r auf text.replace("&", "&amp;")
    setze r auf r.replace("<", "&lt;")
    setze r auf r.replace(">", "&gt;")
    gib_zurück r

# === Inline Parsing ===
# Bold, Italic, Code, Links, Bilder

funktion parse_inline(text):
    setze result auf ""
    setze i auf 0
    setze n auf länge(text)

    solange i < n:
        setze c auf text[i]

        # Inline Code: `code`
        wenn c == "`":
            setze ende auf i + 1
            solange ende < n:
                wenn text[ende] == "`":
                    stopp
                setze ende auf ende + 1
            wenn ende < n:
                setze inhalt auf text.slice(i + 1, ende)
                setze result auf result + "<code>" + html_escape(inhalt) + "</code>"
                setze i auf ende + 1
                weiter

        # Bilder: ![alt](url)
        wenn c == "!":
            wenn i + 1 < n:
                wenn text[i + 1] == "[":
                    # Suche ]
                    setze close_br auf i + 2
                    solange close_br < n:
                        wenn text[close_br] == "]":
                            stopp
                        setze close_br auf close_br + 1
                    wenn close_br < n:
                        wenn close_br + 1 < n:
                            wenn text[close_br + 1] == "(":
                                setze close_par auf close_br + 2
                                solange close_par < n:
                                    wenn text[close_par] == ")":
                                        stopp
                                    setze close_par auf close_par + 1
                                wenn close_par < n:
                                    setze alt auf text.slice(i + 2, close_br)
                                    setze url auf text.slice(close_br + 2, close_par)
                                    setze result auf result + "<img src=\"" + url + "\" alt=\"" + alt + "\">"
                                    setze i auf close_par + 1
                                    weiter

        # Links: [text](url)
        wenn c == "[":
            setze close_br auf i + 1
            solange close_br < n:
                wenn text[close_br] == "]":
                    stopp
                setze close_br auf close_br + 1
            wenn close_br < n:
                wenn close_br + 1 < n:
                    wenn text[close_br + 1] == "(":
                        setze close_par auf close_br + 2
                        solange close_par < n:
                            wenn text[close_par] == ")":
                                stopp
                            setze close_par auf close_par + 1
                        wenn close_par < n:
                            setze linktext auf text.slice(i + 1, close_br)
                            setze url auf text.slice(close_br + 2, close_par)
                            setze result auf result + "<a href=\"" + url + "\">" + html_escape(linktext) + "</a>"
                            setze i auf close_par + 1
                            weiter

        # Bold: **text**
        wenn c == "*":
            wenn i + 1 < n:
                wenn text[i + 1] == "*":
                    # Suche **
                    setze ende auf i + 2
                    solange ende < n - 1:
                        wenn text[ende] == "*":
                            wenn text[ende + 1] == "*":
                                stopp
                        setze ende auf ende + 1
                    wenn ende < n - 1:
                        setze inhalt auf text.slice(i + 2, ende)
                        setze result auf result + "<strong>" + html_escape(inhalt) + "</strong>"
                        setze i auf ende + 2
                        weiter

            # Italic: *text*
            setze ende auf i + 1
            solange ende < n:
                wenn text[ende] == "*":
                    stopp
                setze ende auf ende + 1
            wenn ende < n:
                setze inhalt auf text.slice(i + 1, ende)
                setze result auf result + "<em>" + html_escape(inhalt) + "</em>"
                setze i auf ende + 1
                weiter

        # Normales Zeichen — escape
        wenn c == "&":
            setze result auf result + "&amp;"
        sonst:
            wenn c == "<":
                setze result auf result + "&lt;"
            sonst:
                wenn c == ">":
                    setze result auf result + "&gt;"
                sonst:
                    setze result auf result + c
        setze i auf i + 1

    gib_zurück result

# === Block Parsing ===
# Zeilen-basiertes Parsing

funktion zaehle_hash(zeile):
    setze n auf 0
    solange n < länge(zeile):
        wenn zeile[n] != "#":
            stopp
        setze n auf n + 1
    gib_zurück n

funktion ist_leer(zeile):
    setze t auf zeile.trim()
    gib_zurück länge(t) == 0

funktion beginnt_mit(zeile, prefix):
    wenn länge(prefix) > länge(zeile):
        gib_zurück falsch
    setze i auf 0
    solange i < länge(prefix):
        wenn zeile[i] != prefix[i]:
            gib_zurück falsch
        setze i auf i + 1
    gib_zurück wahr

funktion markdown_zu_html(md):
    setze zeilen auf md.split("\n")
    setze output auf ""
    setze i auf 0
    setze n auf länge(zeilen)
    setze in_list auf falsch
    setze in_olist auf falsch
    setze in_quote auf falsch
    setze in_code auf falsch

    solange i < n:
        setze zeile auf zeilen[i]

        # Code-Block: ```
        wenn beginnt_mit(zeile.trim(), "```"):
            wenn in_code:
                setze output auf output + "</code></pre>\n"
                setze in_code auf falsch
            sonst:
                setze output auf output + "<pre><code>"
                setze in_code auf wahr
            setze i auf i + 1
            weiter

        wenn in_code:
            setze output auf output + html_escape(zeile) + "\n"
            setze i auf i + 1
            weiter

        # Leerzeile
        wenn ist_leer(zeile):
            wenn in_list:
                setze output auf output + "</ul>\n"
                setze in_list auf falsch
            wenn in_olist:
                setze output auf output + "</ol>\n"
                setze in_olist auf falsch
            wenn in_quote:
                setze output auf output + "</blockquote>\n"
                setze in_quote auf falsch
            setze i auf i + 1
            weiter

        # Horizontale Regel: ---
        wenn zeile.trim() == "---":
            wenn in_list:
                setze output auf output + "</ul>\n"
                setze in_list auf falsch
            wenn in_olist:
                setze output auf output + "</ol>\n"
                setze in_olist auf falsch
            wenn in_quote:
                setze output auf output + "</blockquote>\n"
                setze in_quote auf falsch
            setze output auf output + "<hr>\n"
            setze i auf i + 1
            weiter

        # Heading: # ## ###
        wenn beginnt_mit(zeile, "#"):
            setze level auf zaehle_hash(zeile)
            wenn level >= 1:
                wenn level <= 6:
                    wenn in_list:
                        setze output auf output + "</ul>\n"
                        setze in_list auf falsch
                    wenn in_olist:
                        setze output auf output + "</ol>\n"
                        setze in_olist auf falsch
                    wenn in_quote:
                        setze output auf output + "</blockquote>\n"
                        setze in_quote auf falsch
                    setze rest auf zeile.slice(level, länge(zeile)).trim()
                    setze tag auf "h" + text(level)
                    setze output auf output + "<" + tag + ">" + parse_inline(rest) + "</" + tag + ">\n"
                    setze i auf i + 1
                    weiter

        # Blockquote: >
        wenn beginnt_mit(zeile, ">"):
            wenn in_list:
                setze output auf output + "</ul>\n"
                setze in_list auf falsch
            wenn in_olist:
                setze output auf output + "</ol>\n"
                setze in_olist auf falsch
            wenn nicht in_quote:
                setze output auf output + "<blockquote>\n"
                setze in_quote auf wahr
            setze rest auf zeile.slice(1, länge(zeile)).trim()
            setze output auf output + parse_inline(rest) + "\n"
            setze i auf i + 1
            weiter

        # Unordered List: - oder *
        wenn beginnt_mit(zeile, "- "):
            wenn in_quote:
                setze output auf output + "</blockquote>\n"
                setze in_quote auf falsch
            wenn in_olist:
                setze output auf output + "</ol>\n"
                setze in_olist auf falsch
            wenn nicht in_list:
                setze output auf output + "<ul>\n"
                setze in_list auf wahr
            setze rest auf zeile.slice(2, länge(zeile))
            setze output auf output + "<li>" + parse_inline(rest) + "</li>\n"
            setze i auf i + 1
            weiter

        wenn beginnt_mit(zeile, "* "):
            wenn in_quote:
                setze output auf output + "</blockquote>\n"
                setze in_quote auf falsch
            wenn in_olist:
                setze output auf output + "</ol>\n"
                setze in_olist auf falsch
            wenn nicht in_list:
                setze output auf output + "<ul>\n"
                setze in_list auf wahr
            setze rest auf zeile.slice(2, länge(zeile))
            setze output auf output + "<li>" + parse_inline(rest) + "</li>\n"
            setze i auf i + 1
            weiter

        # Ordered List: 1. 2. etc.
        wenn länge(zeile) >= 3:
            setze erstes auf zeile[0]
            wenn erstes >= "0":
                wenn erstes <= "9":
                    wenn zeile[1] == ".":
                        wenn zeile[2] == " ":
                            wenn in_quote:
                                setze output auf output + "</blockquote>\n"
                                setze in_quote auf falsch
                            wenn in_list:
                                setze output auf output + "</ul>\n"
                                setze in_list auf falsch
                            wenn nicht in_olist:
                                setze output auf output + "<ol>\n"
                                setze in_olist auf wahr
                            setze rest auf zeile.slice(3, länge(zeile))
                            setze output auf output + "<li>" + parse_inline(rest) + "</li>\n"
                            setze i auf i + 1
                            weiter

        # Paragraph (Default)
        # Schliesse offene Listen
        wenn in_list:
            setze output auf output + "</ul>\n"
            setze in_list auf falsch
        wenn in_olist:
            setze output auf output + "</ol>\n"
            setze in_olist auf falsch
        wenn in_quote:
            setze output auf output + "</blockquote>\n"
            setze in_quote auf falsch

        setze output auf output + "<p>" + parse_inline(zeile) + "</p>\n"
        setze i auf i + 1

    # Offene Tags schliessen
    wenn in_list:
        setze output auf output + "</ul>\n"
    wenn in_olist:
        setze output auf output + "</ol>\n"
    wenn in_quote:
        setze output auf output + "</blockquote>\n"
    wenn in_code:
        setze output auf output + "</code></pre>\n"

    gib_zurück output

# === Tests ===
setze ok auf 0
setze fail auf 0

funktion enthaelt(gross, klein):
    wenn länge(klein) > länge(gross):
        gib_zurück falsch
    setze i auf 0
    solange i <= länge(gross) - länge(klein):
        setze treffer auf wahr
        setze j auf 0
        solange j < länge(klein):
            wenn gross[i + j] != klein[j]:
                setze treffer auf falsch
                stopp
            setze j auf j + 1
        wenn treffer:
            gib_zurück wahr
        setze i auf i + 1
    gib_zurück falsch

# Test: Heading
setze r1 auf markdown_zu_html("# Titel")
wenn enthaelt(r1, "<h1>Titel</h1>"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL Heading: {r1}"

# Test: Bold
setze r2 auf markdown_zu_html("Hier **fett** Text")
wenn enthaelt(r2, "<strong>fett</strong>"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL Bold: {r2}"

# Test: Italic
setze r3 auf markdown_zu_html("Hier *kursiv* Text")
wenn enthaelt(r3, "<em>kursiv</em>"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL Italic: {r3}"

# Test: Inline Code
setze r4 auf markdown_zu_html("Hier `code` Text")
wenn enthaelt(r4, "<code>code</code>"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL Code: {r4}"

# Test: Link
setze r5 auf markdown_zu_html("Ein [Link](https://moo.dev)")
wenn enthaelt(r5, "<a href=\"https://moo.dev\">Link</a>"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL Link: {r5}"

# Test: Bild
setze r6 auf markdown_zu_html("![Alt](bild.png)")
wenn enthaelt(r6, "<img src=\"bild.png\" alt=\"Alt\">"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL Bild: {r6}"

# Test: Unordered List
setze r7 auf markdown_zu_html("- Eins\n- Zwei")
wenn enthaelt(r7, "<ul>"):
    wenn enthaelt(r7, "<li>Eins</li>"):
        ok += 1
    sonst:
        fail += 1
sonst:
    fail += 1

# Test: Ordered List
setze r8 auf markdown_zu_html("1. Eins\n2. Zwei")
wenn enthaelt(r8, "<ol>"):
    wenn enthaelt(r8, "<li>Eins</li>"):
        ok += 1
    sonst:
        fail += 1
sonst:
    fail += 1

# Test: Blockquote
setze r9 auf markdown_zu_html("> Zitat")
wenn enthaelt(r9, "<blockquote>"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL Quote: {r9}"

# Test: HR
setze r10 auf markdown_zu_html("---")
wenn enthaelt(r10, "<hr>"):
    ok += 1
sonst:
    fail += 1
    zeige f"FAIL HR: {r10}"

# Test: HTML-Escape in Paragraphs
setze r11 auf markdown_zu_html("A < B & C > D")
wenn enthaelt(r11, "&lt;"):
    wenn enthaelt(r11, "&amp;"):
        wenn enthaelt(r11, "&gt;"):
            ok += 1
        sonst:
            fail += 1
    sonst:
        fail += 1
sonst:
    fail += 1

# Test: Kombiniert
setze r12 auf markdown_zu_html("# Titel\n\nText mit **fett** und *kursiv*.")
wenn enthaelt(r12, "<h1>Titel</h1>"):
    wenn enthaelt(r12, "<strong>fett</strong>"):
        wenn enthaelt(r12, "<em>kursiv</em>"):
            ok += 1
        sonst:
            fail += 1
    sonst:
        fail += 1
sonst:
    fail += 1

zeige ""
zeige f"Markdown Tests: {ok} OK, {fail} FAIL"
wenn fail == 0:
    zeige "ALLE MARKDOWN-TESTS BESTANDEN!"

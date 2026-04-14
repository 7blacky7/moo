# Markdown-CMS in moo — Stresstest
# Statische Blog-Engine: liest .md-Posts aus einem Verzeichnis,
# parst Front-Matter + Markdown, rendert Index/Post/Tag/RSS.
#
# Posts-Format (Front-Matter zwischen --- und ---):
#   ---
#   title: Mein Post
#   date: 2024-06-15
#   tags: moo, rust, hello
#   ---
#   # Ueberschrift
#
#   Inhalt mit **fett** und *kursiv*.

# ============================================================
# MINI-MARKDOWN-PARSER (inline, fuer Test-Reuse)
# ============================================================

klasse MdParser:
    funktion erstelle():
        selbst.out = ""

    funktion escape(t):
        setze r auf t.ersetzen("&", "&amp;")
        setze r auf r.ersetzen("<", "&lt;")
        setze r auf r.ersetzen(">", "&gt;")
        gib_zurück r

    # Manuelle Inline-Formatierung (regex_replace hat keine Groups)
    funktion inline(text):
        setze out auf ""
        setze idx auf 0
        setze n auf länge(text)
        solange idx < n:
            setze c auf text[idx]

            # Bold **x**
            wenn c == "*" und idx + 1 < n und text[idx + 1] == "*":
                setze ende auf idx + 2
                solange ende < n - 1:
                    wenn text[ende] == "*" und text[ende + 1] == "*":
                        stopp
                    setze ende auf ende + 1
                wenn ende < n - 1:
                    setze inhalt auf text.slice(idx + 2, ende)
                    setze out auf out + "<strong>" + selbst.escape(inhalt) + "</strong>"
                    setze idx auf ende + 2
                    weiter

            # Italic *x*
            wenn c == "*":
                setze ende auf idx + 1
                solange ende < n:
                    wenn text[ende] == "*":
                        stopp
                    setze ende auf ende + 1
                wenn ende < n:
                    setze inhalt auf text.slice(idx + 1, ende)
                    setze out auf out + "<em>" + selbst.escape(inhalt) + "</em>"
                    setze idx auf ende + 1
                    weiter

            # Inline code `x`
            wenn c == "`":
                setze ende auf idx + 1
                solange ende < n:
                    wenn text[ende] == "`":
                        stopp
                    setze ende auf ende + 1
                wenn ende < n:
                    setze inhalt auf text.slice(idx + 1, ende)
                    setze out auf out + "<code>" + selbst.escape(inhalt) + "</code>"
                    setze idx auf ende + 1
                    weiter

            # Normaler Text: Escape-char
            wenn c == "&":
                setze out auf out + "&amp;"
            sonst wenn c == "<":
                setze out auf out + "&lt;"
            sonst wenn c == ">":
                setze out auf out + "&gt;"
            sonst:
                setze out auf out + c
            setze idx auf idx + 1
        gib_zurück out

    funktion rendern(md):
        setze zeilen auf md.teilen("\n")
        setze out auf []
        setze in_list auf falsch
        setze in_code auf falsch
        setze absatz auf []
        setze idx auf 0
        setze n auf länge(zeilen)
        solange idx < n:
            setze z auf zeilen[idx]

            wenn in_code:
                wenn z.trim() == "```":
                    out.hinzufügen("</code></pre>")
                    setze in_code auf falsch
                sonst:
                    out.hinzufügen(selbst.escape(z))
                setze idx auf idx + 1
                weiter

            wenn z.trim() == "```":
                wenn länge(absatz) > 0:
                    out.hinzufügen("<p>" + absatz.verbinden(" ") + "</p>")
                    setze absatz auf []
                out.hinzufügen("<pre><code>")
                setze in_code auf wahr
                setze idx auf idx + 1
                weiter

            wenn länge(z) == 0:
                wenn länge(absatz) > 0:
                    out.hinzufügen("<p>" + absatz.verbinden(" ") + "</p>")
                    setze absatz auf []
                wenn in_list:
                    out.hinzufügen("</ul>")
                    setze in_list auf falsch
                setze idx auf idx + 1
                weiter

            # Heading
            wenn z.slice(0, 4) == "### ":
                wenn länge(absatz) > 0:
                    out.hinzufügen("<p>" + absatz.verbinden(" ") + "</p>")
                    setze absatz auf []
                out.hinzufügen("<h3>" + selbst.inline(z.slice(4, länge(z))) + "</h3>")
                setze idx auf idx + 1
                weiter
            wenn z.slice(0, 3) == "## ":
                wenn länge(absatz) > 0:
                    out.hinzufügen("<p>" + absatz.verbinden(" ") + "</p>")
                    setze absatz auf []
                out.hinzufügen("<h2>" + selbst.inline(z.slice(3, länge(z))) + "</h2>")
                setze idx auf idx + 1
                weiter
            wenn z.slice(0, 2) == "# ":
                wenn länge(absatz) > 0:
                    out.hinzufügen("<p>" + absatz.verbinden(" ") + "</p>")
                    setze absatz auf []
                out.hinzufügen("<h1>" + selbst.inline(z.slice(2, länge(z))) + "</h1>")
                setze idx auf idx + 1
                weiter

            # Liste "- "
            wenn z.slice(0, 2) == "- ":
                wenn länge(absatz) > 0:
                    out.hinzufügen("<p>" + absatz.verbinden(" ") + "</p>")
                    setze absatz auf []
                wenn in_list == falsch:
                    out.hinzufügen("<ul>")
                    setze in_list auf wahr
                out.hinzufügen("<li>" + selbst.inline(z.slice(2, länge(z))) + "</li>")
                setze idx auf idx + 1
                weiter

            # Absatz-Zeile
            wenn in_list:
                out.hinzufügen("</ul>")
                setze in_list auf falsch
            absatz.hinzufügen(selbst.inline(z))
            setze idx auf idx + 1

        wenn länge(absatz) > 0:
            out.hinzufügen("<p>" + absatz.verbinden(" ") + "</p>")
        wenn in_list:
            out.hinzufügen("</ul>")
        wenn in_code:
            out.hinzufügen("</code></pre>")

        gib_zurück out.verbinden("\n")


# ============================================================
# POST + CMS
# ============================================================

klasse Post:
    funktion erstelle():
        selbst.slug = ""
        selbst.titel = ""
        selbst.datum = ""
        selbst.tags = []
        selbst.inhalt_md = ""
        selbst.inhalt_html = ""


klasse CMS:
    funktion erstelle():
        selbst.posts = []
        selbst.by_slug = {}
        selbst.by_tag = {}
        selbst.parser = neu MdParser()

    # Parst einen Markdown-Text mit YAML-artigem Front-Matter
    funktion parse_post(slug, text):
        setze p auf neu Post()
        p.slug = slug
        setze zeilen auf text.teilen("\n")
        setze idx auf 0
        # Front-Matter: "---" zu "---"
        wenn länge(zeilen) > 0 und zeilen[0].trim() == "---":
            setze idx auf 1
            solange idx < länge(zeilen):
                wenn zeilen[idx].trim() == "---":
                    setze idx auf idx + 1
                    stopp
                setze zeile auf zeilen[idx]
                setze doppelpunkt auf selbst.finde_char(zeile, ":")
                wenn doppelpunkt > 0:
                    setze schluessel auf zeile.slice(0, doppelpunkt).trim()
                    setze wert auf zeile.slice(doppelpunkt + 1, länge(zeile)).trim()
                    wenn schluessel == "title":
                        p.titel = wert
                    wenn schluessel == "date":
                        p.datum = wert
                    wenn schluessel == "tags":
                        setze teile auf wert.teilen(",")
                        setze liste auf []
                        für t in teile:
                            setze gt auf t.trim()
                            wenn länge(gt) > 0:
                                liste.hinzufügen(gt)
                        p.tags = liste
                setze idx auf idx + 1

        # Rest ist Markdown
        setze md_zeilen auf []
        solange idx < länge(zeilen):
            md_zeilen.hinzufügen(zeilen[idx])
            setze idx auf idx + 1
        setze md auf md_zeilen.verbinden("\n")
        p.inhalt_md = md
        p.inhalt_html = selbst.parser.rendern(md)
        gib_zurück p

    funktion finde_char(text, c):
        setze idx auf 0
        setze n auf länge(text)
        solange idx < n:
            wenn text[idx] == c:
                gib_zurück idx
            setze idx auf idx + 1
        gib_zurück -1

    funktion lade_post_datei(slug, pfad):
        setze txt auf datei_lesen(pfad)
        setze p auf selbst.parse_post(slug, txt)
        selbst.posts.hinzufügen(p)
        selbst.by_slug[slug] = p
        für t in p.tags:
            wenn selbst.by_tag.hat(t):
                setze l auf selbst.by_tag[t]
                l.hinzufügen(p)
                selbst.by_tag[t] = l
            sonst:
                selbst.by_tag[t] = [p]

    funktion lade_verzeichnis(pfad):
        setze dateien auf verzeichnis_liste(pfad)
        für datei in dateien:
            setze n auf länge(datei)
            wenn n > 3 und datei.slice(n - 3, n) == ".md":
                setze slug auf datei.slice(0, n - 3)
                setze voller auf pfad + "/" + datei
                selbst.lade_post_datei(slug, voller)
        selbst.sortiere_nach_datum()

    # Sortiert posts[] nach datum DESC (neueste zuerst)
    funktion sortiere_nach_datum():
        setze n auf länge(selbst.posts)
        setze idx auf 1
        solange idx < n:
            setze jdx auf idx
            solange jdx > 0:
                wenn selbst.posts[jdx - 1].datum < selbst.posts[jdx].datum:
                    setze tmp auf selbst.posts[jdx - 1]
                    selbst.posts[jdx - 1] = selbst.posts[jdx]
                    selbst.posts[jdx] = tmp
                    setze jdx auf jdx - 1
                sonst:
                    stopp
            setze idx auf idx + 1

    # ---- Renderer ----

    funktion rendere_index():
        setze h auf "<!DOCTYPE html><html><head><title>Blog</title></head><body>"
        setze h auf h + "<h1>Alle Posts</h1><ul>"
        für p in selbst.posts:
            setze h auf h + "<li><a href=\"/post/" + p.slug + "\">"
            setze h auf h + p.titel + "</a> <small>" + p.datum + "</small></li>"
        setze h auf h + "</ul>"
        setze h auf h + "<h2>Tags</h2><ul>"
        für t in selbst.by_tag.schlüssel():
            setze liste auf selbst.by_tag[t]
            setze h auf h + "<li><a href=\"/tag/" + t + "\">" + t + "</a> (" + text(länge(liste)) + ")</li>"
        setze h auf h + "</ul></body></html>"
        gib_zurück h

    funktion rendere_post(slug):
        wenn selbst.by_slug.hat(slug) == falsch:
            gib_zurück nichts
        setze p auf selbst.by_slug[slug]
        setze h auf "<!DOCTYPE html><html><head><title>" + p.titel
        setze h auf h + "</title></head><body>"
        setze h auf h + "<a href=\"/\">&larr; Alle Posts</a>"
        setze h auf h + "<article>"
        setze h auf h + "<h1>" + p.titel + "</h1>"
        setze h auf h + "<time>" + p.datum + "</time>"
        setze h auf h + "<div class=\"tags\">"
        für t in p.tags:
            setze h auf h + "<a href=\"/tag/" + t + "\">#" + t + "</a> "
        setze h auf h + "</div>"
        setze h auf h + p.inhalt_html
        setze h auf h + "</article></body></html>"
        gib_zurück h

    funktion rendere_tag(tag):
        wenn selbst.by_tag.hat(tag) == falsch:
            gib_zurück nichts
        setze liste auf selbst.by_tag[tag]
        setze h auf "<!DOCTYPE html><html><head><title>Tag: " + tag
        setze h auf h + "</title></head><body>"
        setze h auf h + "<h1>Tag: " + tag + "</h1><ul>"
        für p in liste:
            setze h auf h + "<li><a href=\"/post/" + p.slug + "\">"
            setze h auf h + p.titel + "</a></li>"
        setze h auf h + "</ul></body></html>"
        gib_zurück h

    funktion rendere_rss():
        setze r auf "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        setze r auf r + "<rss version=\"2.0\"><channel>"
        setze r auf r + "<title>moo Blog</title>"
        setze r auf r + "<link>http://localhost:8080/</link>"
        setze r auf r + "<description>Markdown-CMS in moo</description>"
        für p in selbst.posts:
            setze r auf r + "<item>"
            setze r auf r + "<title>" + p.titel + "</title>"
            setze r auf r + "<link>http://localhost:8080/post/" + p.slug + "</link>"
            setze r auf r + "<pubDate>" + p.datum + "</pubDate>"
            für t in p.tags:
                setze r auf r + "<category>" + t + "</category>"
            setze r auf r + "</item>"
        setze r auf r + "</channel></rss>"
        gib_zurück r

    # Routing-Logik: gibt {status, body} zurueck
    funktion route(pfad):
        wenn pfad == "/":
            gib_zurück {"status": 200, "body": selbst.rendere_index()}
        wenn pfad == "/rss":
            gib_zurück {"status": 200, "body": selbst.rendere_rss()}
        wenn länge(pfad) > 6 und pfad.slice(0, 6) == "/post/":
            setze slug auf pfad.slice(6, länge(pfad))
            setze html auf selbst.rendere_post(slug)
            wenn html == nichts:
                gib_zurück {"status": 404, "body": "Post nicht gefunden"}
            gib_zurück {"status": 200, "body": html}
        wenn länge(pfad) > 5 und pfad.slice(0, 5) == "/tag/":
            setze tag auf pfad.slice(5, länge(pfad))
            setze html auf selbst.rendere_tag(tag)
            wenn html == nichts:
                gib_zurück {"status": 404, "body": "Tag nicht gefunden"}
            gib_zurück {"status": 200, "body": html}
        gib_zurück {"status": 404, "body": "Not Found"}

    # Hot-Reload: komplett neu laden (einfachste Variante)
    funktion reload(pfad):
        selbst.posts = []
        selbst.by_slug = {}
        selbst.by_tag = {}
        selbst.lade_verzeichnis(pfad)


# ============================================================
# TEST-HELFER + DATEN
# ============================================================

funktion schreibe_post(pfad, inhalt):
    datei_schreiben(pfad, inhalt)

funktion erzeuge_test_posts(verz):
    datei_schreiben(verz + "/hello.md", "---\ntitle: Hallo Welt\ndate: 2024-06-15\ntags: moo, hallo\n---\n# Hallo\n\nEin erster Post mit **fett** und *kursiv*.\n\n- Punkt eins\n- Punkt zwei\n")
    datei_schreiben(verz + "/zweiter.md", "---\ntitle: Zweiter Post\ndate: 2024-07-01\ntags: moo, tutorial\n---\n## Header 2\n\nEin Absatz mit `code`.\n\n```\nsetze x auf 1\n```\n")
    datei_schreiben(verz + "/dritter.md", "---\ntitle: Der Dritte\ndate: 2024-05-20\ntags: rust\n---\n### Kleiner Header\n\nKurztext.\n")
    datei_schreiben(verz + "/vierter.md", "---\ntitle: Nummer Vier\ndate: 2024-08-10\ntags: moo\n---\n# Vier\n\nEin weiterer Post.\n")
    datei_schreiben(verz + "/fuenf.md", "---\ntitle: Post Fuenf\ndate: 2024-04-01\ntags: tutorial, moo\n---\n# Fuenf\n\nNoch mehr Text.\n")


klasse TZaehler:
    funktion erstelle():
        selbst.ok = 0
        selbst.fail = 0
    funktion erfolg():
        selbst.ok = selbst.ok + 1
    funktion fehler():
        selbst.fail = selbst.fail + 1

setze TESTS auf neu TZaehler()

funktion check(name, bedingung):
    wenn bedingung:
        zeige "  OK  " + name
        TESTS.erfolg()
    sonst:
        zeige "  FAIL " + name
        TESTS.fehler()


# ============================================================
# TESTS
# ============================================================

zeige "=== Markdown-CMS Tests ==="
zeige ""

# Posts kommen aus dem echten Repo-Verzeichnis beispiele/cms_posts/
# (mit 5 vorbereiteten .md-Dateien committed).
# Hot-Reload-Test schreibt einen neuen Post ins gleiche Verzeichnis
# und raeumt am Ende auf.
setze POSTS_DIR auf "beispiele/cms_posts"
datei_löschen(POSTS_DIR + "/neu.md")

zeige "Test 1: Markdown-Parser einzeln"
setze mdp auf neu MdParser()
setze html auf mdp.rendern("# Titel\n\nEin **fett** und *kursiv* und `code`.\n\n- A\n- B\n")
check("H1 in Output", html.ersetzen("<h1>Titel</h1>", "X") != html)
check("strong in Output", html.ersetzen("<strong>fett</strong>", "X") != html)
check("em in Output", html.ersetzen("<em>kursiv</em>", "X") != html)
check("code in Output", html.ersetzen("<code>code</code>", "X") != html)
check("ul in Output", html.ersetzen("<ul>", "X") != html)

zeige ""
zeige "Test 2: Front-Matter parsen"
setze cms auf neu CMS()
setze p auf cms.parse_post("probe", "---\ntitle: Test Titel\ndate: 2024-01-01\ntags: a, b, c\n---\n# H\n")
check("Titel geparst", p.titel == "Test Titel")
check("Datum geparst", p.datum == "2024-01-01")
check("3 Tags", länge(p.tags) == 3)
check("Tag a", p.tags[0] == "a")
check("HTML enthaelt h1", p.inhalt_html.ersetzen("<h1>H</h1>", "X") != p.inhalt_html)

zeige ""
zeige "Test 3: CMS lade Verzeichnis"
setze cms2 auf neu CMS()
cms2.lade_verzeichnis(POSTS_DIR)
check("5 Posts geladen", länge(cms2.posts) == 5)
check("hello geladen", cms2.by_slug.hat("hello"))
check("3 Tags im Index (moo, tutorial, rust, hallo)", länge(cms2.by_tag.schlüssel()) == 4)

zeige ""
zeige "Test 4: Sortierung nach Datum DESC"
check("neuester zuerst (vierter 2024-08-10)", cms2.posts[0].slug == "vierter")
check("aeltester zuletzt (fuenf 2024-04-01)", cms2.posts[4].slug == "fuenf")

zeige ""
zeige "Test 5: Routing"
setze r auf cms2.route("/")
check("/ Status 200", r["status"] == 200)
check("/ enthaelt Post-Liste", r["body"].ersetzen("Alle Posts", "X") != r["body"])

setze r auf cms2.route("/post/hello")
check("/post/hello Status 200", r["status"] == 200)
check("/post/hello enthaelt titel", r["body"].ersetzen("Hallo Welt", "X") != r["body"])

setze r auf cms2.route("/post/nicht_da")
check("/post/nicht_da → 404", r["status"] == 404)

setze r auf cms2.route("/tag/moo")
check("/tag/moo Status 200", r["status"] == 200)

setze r auf cms2.route("/tag/unbekannt")
check("/tag/unbekannt → 404", r["status"] == 404)

setze r auf cms2.route("/rss")
check("/rss Status 200", r["status"] == 200)
check("/rss ist XML", r["body"].slice(0, 5) == "<?xml")
check("/rss hat channel", r["body"].ersetzen("<channel>", "X") != r["body"])

setze r auf cms2.route("/unbekannt")
check("/unbekannt → 404", r["status"] == 404)

zeige ""
zeige "Test 6: Tag-Filter zaehlt korrekt"
setze moo_posts auf cms2.by_tag["moo"]
check("moo hat 4 Posts", länge(moo_posts) == 4)
setze rust_posts auf cms2.by_tag["rust"]
check("rust hat 1 Post", länge(rust_posts) == 1)

zeige ""
zeige "Test 7: Hot-Reload"
datei_schreiben(POSTS_DIR + "/neu.md", "---\ntitle: Brandneu\ndate: 2024-12-31\ntags: moo\n---\n# Neu\n")
cms2.reload(POSTS_DIR)
check("6 Posts nach Reload", länge(cms2.posts) == 6)
check("neuester jetzt 'neu'", cms2.posts[0].slug == "neu")
check("moo hat jetzt 5 Posts", länge(cms2.by_tag["moo"]) == 5)

# Aufraeumen: neu.md wieder loeschen (damit naechster Lauf 5/5 kriegt)
datei_löschen(POSTS_DIR + "/neu.md")

zeige ""
zeige "=========================================="
zeige "Ergebnis: " + text(TESTS.ok) + " OK, " + text(TESTS.fail) + " FAIL"
zeige "=========================================="

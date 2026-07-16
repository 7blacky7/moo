# Markdown-Parser Tests — erwartet dass markdown_zu_html definiert ist
# NOTE: Kopiere die Funktionen aus markdown.moo

# HTML-Escape
funktion html_escape(text):
    setze r auf text.replace("&", "&amp;")
    setze r auf r.replace("<", "&lt;")
    setze r auf r.replace(">", "&gt;")
    gib_zurück r

setze ok auf 0
setze fail auf 0

# Test 1: HTML-Escape
wenn html_escape("<script>") == "&lt;script&gt;":
    ok += 1
sonst:
    fail += 1

wenn html_escape("A & B") == "A &amp; B":
    ok += 1
sonst:
    fail += 1

zeige f"HTML-Escape Tests: {ok} OK, {fail} FAIL"

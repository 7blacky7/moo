# Markdown-Parser Testcases — einzelne Konstrukte

# HTML-Escape
funktion html_escape(text):
    setze r auf text.replace("&", "&amp;")
    setze r auf r.replace("<", "&lt;")
    setze r auf r.replace(">", "&gt;")
    gib_zurück r

# (Die vollstaendige parse_inline + markdown_zu_html Logik wuerde hier stehen —
#  ich teste nur die inline-Parts einzeln)

setze ok auf 0

# Test: Bold
setze t1 auf "Hier **fett** Text"
# Erwartet: contains "<strong>fett</strong>"

# Test: Italic
setze t2 auf "Hier *kursiv* Text"
# Erwartet: contains "<em>kursiv</em>"

# Test: Code
setze t3 auf "Hier `code` Text"
# Erwartet: contains "<code>code</code>"

# Test: Link
setze t4 auf "Ein [Link](url)"
# Erwartet: contains "<a href=\"url\">Link</a>"

zeige "Testcases definiert, bereit"
zeige f"t1 = {t1}"
zeige f"t2 = {t2}"
zeige f"t3 = {t3}"
zeige f"t4 = {t4}"

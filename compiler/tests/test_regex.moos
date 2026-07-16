# Regex Test — POSIX regex
setze m auf regex("[0-9]+")

# passt() — Match prüfen
zeige passt("abc123def", m)

# finde() — Ersten Treffer extrahieren
zeige finde("abc123def", m)

# finde_alle() — Alle Treffer
setze zahlen auf finde_alle("a1b22c333", m)
zeige zahlen

# ersetze() — Ersten Treffer ersetzen
zeige ersetze("hallo 42 welt", m, "XX")

# Email-Pattern
setze email auf regex("[a-zA-Z0-9]+@[a-zA-Z0-9]+\\.[a-zA-Z]+")
zeige passt("test@example.com", email)
zeige finde("Schreib an info@moo.dev bitte", email)
